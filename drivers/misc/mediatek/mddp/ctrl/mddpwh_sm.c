/*
 * mddpwh_sm.c - MDDPWH (WiFi Hotspot) state machine.
 *
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "mddp_ctrl.h"
#include "mddp_filter.h"

#include "mddp_dev.h"
#include "mddp_if.h"
#include "mddp_ipc.h"
#include "mddp_sm.h"
#include "mddp_wifi_def.h"

//------------------------------------------------------------------------------
// Struct definition.
// -----------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Global variables.
//------------------------------------------------------------------------------
static struct wfpm_deactivate_md_func_rsp_t deact_rsp_metadata_s;

//------------------------------------------------------------------------------
// Private variables.
//------------------------------------------------------------------------------
static struct mddp_md_cfg_t mddpw_md_cfg_s = {
	MDFPM_AP_USER_ID,
	MDFPM_USER_ID_WFPM,
};

struct mddp_sm_entry_t *prev_mddpwh_state_machines_s;
static struct timer_list mddpw_timer;
static struct work_struct mddpw_reset_workq;
static uint8_t mddpw_reset_ongoing;

#define MDDP_WIFI_NETIF_ID 0x500 /* copy from MD IPC_NETIF_ID_MCIF_BEGIN */

//------------------------------------------------------------------------------
// Private helper macro.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Private functions.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// External functions.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Public functions - MDDPWH (WiFi) state machine functions
//------------------------------------------------------------------------------
void mddpwh_sm_enable(struct mddp_app_t *app)
{
	struct mddp_md_msg_t                   *md_msg;
	struct wfpm_enable_md_func_req_t       *enable_req;
	struct wfpm_smem_info_t                *smem_info;
	uint32_t                                smem_num;

	// 1. Send ENABLE to WiFi
	if (app->drv_hdlr.change_state != NULL)
		app->drv_hdlr.change_state(MDDP_STATE_ENABLING, NULL, NULL);

	if (wfpm_ipc_get_smem_list((void **)&smem_info, &smem_num)) {
		pr_notice("%s: Failed to get smem info!\n", __func__);
		smem_num = 0;
	}

	// 2. Send ENABLE to MD
	md_msg = kzalloc(sizeof(struct mddp_md_msg_t) +
			 sizeof(struct wfpm_enable_md_func_req_t) +
			smem_num * sizeof(struct wfpm_smem_info_t), GFP_KERNEL);

	if (unlikely(!md_msg)) {
		WARN_ON(1);
		return;
	}

	md_msg->msg_id = IPC_MSG_ID_WFPM_ENABLE_MD_FAST_PATH_REQ;
	md_msg->data_len = sizeof(struct wfpm_enable_md_func_req_t) +
		smem_num * sizeof(struct wfpm_smem_info_t);
	enable_req = (struct wfpm_enable_md_func_req_t *)&(md_msg->data);
	enable_req->mode = WFPM_FUNC_MODE_TETHER;
	enable_req->version = __MDDP_VERSION__;
	enable_req->smem_num = smem_num;

	memcpy(&(enable_req->smem_info), smem_info,
			smem_num * sizeof(struct wfpm_smem_info_t));
	mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

void mddpwh_sm_rsp_enable_ok(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_enable_t            enable;

	// 1. Send RSP to WiFi
	app->drv_hdlr.change_state(app->state, NULL, NULL);

	// 2. Send RSP to upper module.
	mddp_dev_response(app->type, MDDP_CMCMD_ENABLE_RSP,
			true, (uint8_t *)&enable, sizeof(enable));
}

void mddpwh_sm_rsp_enable_fail(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_enable_t    enable;

	// 1. Send RSP to WiFi
	app->drv_hdlr.change_state(app->state, NULL, NULL);

	// 2. Send RSP to upper module.
	mddp_dev_response(app->type, MDDP_CMCMD_ENABLE_RSP,
			false, (uint8_t *)&enable, sizeof(enable));
}

void mddpwh_sm_disable(struct mddp_app_t *app)
{
	struct mddp_md_msg_t                   *md_msg;
	struct wfpm_md_fast_path_common_req_t  *disable_req;

	// 1. Send DISABLE to WiFi
	app->drv_hdlr.change_state(MDDP_STATE_DISABLING, NULL, NULL);

	// 2. Send DISABLE to MD
	md_msg = kzalloc(sizeof(struct mddp_md_msg_t) +
			sizeof(struct wfpm_md_fast_path_common_req_t),
			GFP_KERNEL);
	if (unlikely(!md_msg)) {
		pr_notice("%s: Failed to alloc md_msg bug!\n", __func__);
		WARN_ON(1);
		return;
	}

	disable_req = (struct wfpm_md_fast_path_common_req_t *)&(md_msg->data);
	disable_req->mode = WFPM_FUNC_MODE_TETHER;

	md_msg->msg_id = IPC_MSG_ID_WFPM_DISABLE_MD_FAST_PATH_REQ;
	md_msg->data_len = sizeof(struct wfpm_md_fast_path_common_req_t);
	mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

void mddpwh_sm_rsp_disable(struct mddp_app_t *app)
{
	// 1. Send RSP to WiFi
	app->drv_hdlr.change_state(app->state, NULL, NULL);

	// 2. NO NEED to send RSP to upper module.

}

void mddpwh_sm_act(struct mddp_app_t *app)
{
	struct mddp_md_msg_t                 *md_msg;
	struct wfpm_activate_md_func_req_t   *act_req;

	// 1. Register filter model
	mddp_f_dev_add_wan_dev(app->ap_cfg.ul_dev_name);
	mddp_f_dev_add_lan_dev(app->ap_cfg.dl_dev_name, MDDP_WIFI_NETIF_ID);

	// 2. Send ACTIVATING to WiFi
	app->drv_hdlr.change_state(MDDP_STATE_ACTIVATING, NULL, NULL);

	// 3. Send ACTIVATING to MD
	md_msg = kzalloc(sizeof(struct mddp_md_msg_t) +
		sizeof(struct wfpm_activate_md_func_req_t), GFP_KERNEL);

	if (unlikely(!md_msg)) {
		WARN_ON(1);
		return;
	}

	act_req = (struct wfpm_activate_md_func_req_t *)&(md_msg->data);
	act_req->mode = WFPM_FUNC_MODE_TETHER;

	md_msg->msg_id = IPC_MSG_ID_WFPM_ACTIVATE_MD_FAST_PATH_REQ;
	md_msg->data_len = sizeof(struct wfpm_activate_md_func_req_t);
	mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

void mddpwh_sm_rsp_act_ok(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_act_t       act;

	// 1. Send RSP to WiFi
	app->drv_hdlr.change_state(app->state, NULL, NULL);

	// 2. Send RSP to upper module.
	mddp_dev_response(app->type, MDDP_CMCMD_ACT_RSP,
			true, (uint8_t *)&act, sizeof(act));
}

void mddpwh_sm_rsp_act_fail(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_act_t       act;

	// 1. Send RSP to WiFi
	app->drv_hdlr.change_state(app->state, NULL, NULL);

	// 2. Send RSP to upper module.
	mddp_dev_response(app->type, MDDP_CMCMD_ACT_RSP,
			false, (uint8_t *)&act, sizeof(act));
}

void mddpwh_sm_deact(struct mddp_app_t *app)
{
	struct mddp_md_msg_t                 *md_msg;
	struct wfpm_activate_md_func_req_t   *deact_req;

	// 1. Send ACTIVATING to WiFi
	app->drv_hdlr.change_state(MDDP_STATE_DEACTIVATING, NULL, NULL);

	// 2. Send ACTIVATING to MD
	md_msg = kzalloc(sizeof(struct mddp_md_msg_t) +
		sizeof(struct wfpm_activate_md_func_req_t), GFP_KERNEL);

	if (unlikely(!md_msg)) {
		WARN_ON(1);
		return;
	}

	deact_req = (struct wfpm_activate_md_func_req_t *)&(md_msg->data);
	deact_req->mode = WFPM_FUNC_MODE_TETHER;

	md_msg->msg_id = IPC_MSG_ID_WFPM_DEACTIVATE_MD_FAST_PATH_REQ;
	md_msg->data_len = sizeof(struct wfpm_activate_md_func_req_t);
	mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);
}

void mddpwh_sm_rsp_deact(struct mddp_app_t *app)
{
	struct mddp_dev_rsp_deact_t     deact;

	// 1. Register filter model
	mddp_f_dev_del_wan_dev(app->ap_cfg.ul_dev_name);
	mddp_f_dev_del_lan_dev(app->ap_cfg.dl_dev_name);

	// 2. Send RSP to WiFi
	app->drv_hdlr.change_state(app->state, NULL, NULL);

	// 3. Send RSP to upper module.
	mddp_dev_response(app->type, MDDP_CMCMD_DEACT_RSP,
			true, (uint8_t *)&deact, sizeof(deact));
}

//------------------------------------------------------------------------------
// MDDPWH State machine.
//------------------------------------------------------------------------------
static struct mddp_sm_entry_t mddpwh_uninit_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_FUNC_ENABLE,  MDDP_STATE_WAIT_DRV_REG, NULL},
{MDDP_EVT_DRV_REGHDLR,  MDDP_STATE_WAIT_ENABLE,  NULL},
{MDDP_EVT_DUMMY,        MDDP_STATE_UNINIT,       NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_wait_drv_reg_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_DRV_REGHDLR,  MDDP_STATE_ENABLING,     mddpwh_sm_enable},
{MDDP_EVT_DUMMY,        MDDP_STATE_WAIT_DRV_REG, NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_wait_enable_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_FUNC_ENABLE,  MDDP_STATE_ENABLING,     mddpwh_sm_enable},
{MDDP_EVT_DRV_DEREGHDLR, MDDP_STATE_UNINIT,      NULL},
{MDDP_EVT_DUMMY,        MDDP_STATE_WAIT_ENABLE,  NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_enabling_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_MD_RSP_OK,    MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_enable_ok},
{MDDP_EVT_MD_RSP_FAIL,  MDDP_STATE_WAIT_ENABLE,  mddpwh_sm_rsp_enable_fail},
{MDDP_EVT_DUMMY,        MDDP_STATE_ENABLING,     NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_disabling_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_MD_RSP_OK,    MDDP_STATE_WAIT_ENABLE,  mddpwh_sm_rsp_disable},
{MDDP_EVT_MD_RSP_FAIL,  MDDP_STATE_WAIT_ENABLE,  mddpwh_sm_rsp_disable},
{MDDP_EVT_DUMMY,        MDDP_STATE_DISABLING,    NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_drv_disabling_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_DUMMY,        MDDP_STATE_DRV_DISABLING, NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_deactivated_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_FUNC_ACT,     MDDP_STATE_ACTIVATING,   mddpwh_sm_act},
{MDDP_EVT_FUNC_DISABLE, MDDP_STATE_DISABLING,    mddpwh_sm_disable},
{MDDP_EVT_DUMMY,        MDDP_STATE_DEACTIVATED,  NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_activating_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_FUNC_DEACT,   MDDP_STATE_DEACTIVATING, mddpwh_sm_deact},
{MDDP_EVT_MD_RSP_OK,    MDDP_STATE_ACTIVATED,    mddpwh_sm_rsp_act_ok},
{MDDP_EVT_MD_RSP_FAIL,  MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_act_fail},
{MDDP_EVT_DUMMY,        MDDP_STATE_ACTIVATING,   NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_activated_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_FUNC_DEACT,   MDDP_STATE_DEACTIVATING, mddpwh_sm_deact},
{MDDP_EVT_FUNC_DISABLE, MDDP_STATE_DISABLING,    mddpwh_sm_disable},
{MDDP_EVT_DUMMY,        MDDP_STATE_ACTIVATED,    NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_deactivating_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_FUNC_ACT,     MDDP_STATE_ACTIVATING,   mddpwh_sm_act},
{MDDP_EVT_MD_RSP_OK,    MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_deact},
{MDDP_EVT_MD_RSP_FAIL,  MDDP_STATE_DEACTIVATED,  mddpwh_sm_rsp_deact},
{MDDP_EVT_DUMMY,        MDDP_STATE_DEACTIVATING, NULL} /* End of SM. */
};

static struct mddp_sm_entry_t mddpwh_dead_state_machine_s[] = {
/* event                new_state                action */
{MDDP_EVT_DUMMY,        MDDP_STATE_DEACTIVATED,  NULL} /* End of SM. */
};

struct mddp_sm_entry_t *mddpwh_state_machines_s[MDDP_STATE_CNT] = {
	mddpwh_uninit_state_machine_s, /* UNINIT */
	mddpwh_enabling_state_machine_s, /* ENABLING */
	mddpwh_deactivated_state_machine_s, /* DEACTIVATED */
	mddpwh_activating_state_machine_s, /* ACTIVATING */
	mddpwh_activated_state_machine_s, /* ACTIVATED */
	mddpwh_deactivating_state_machine_s, /* DEACTIVATING */
	mddpwh_disabling_state_machine_s, /* DISABLING */
	mddpwh_drv_disabling_state_machine_s, /* DRV DISABLING */
	mddpwh_wait_drv_reg_state_machine_s, /* WAIT DRV REG */
	mddpwh_wait_enable_state_machine_s, /* WAIT ENABLE */
};

//------------------------------------------------------------------------------
// Public functions.
//------------------------------------------------------------------------------

void mddpw_ack_md_reset(struct work_struct *mddp_work)
{
	struct mddp_app_t                *app;
	struct mddp_md_msg_t             *md_msg;
	struct mddpw_md_notify_info_t     md_info;
	uint32_t                          timer;

	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config) {
		pr_notice("%s: app_type(MDDP_APP_TYPE_WH) not configured!\n",
		__func__);
		return;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t), GFP_KERNEL);

	if (unlikely(!md_msg)) {
		WARN_ON(1);
		return;
	}

	md_msg->msg_id = IPC_MSG_ID_WFPM_RESET_IND;
	md_msg->data_len = 0;
	if (unlikely(mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL) >= 0)) {
		pr_info("%s: send_success.\n", __func__);
#ifdef CONFIG_MTK_MDDP_WH_SUPPORT
		app->state_machines[app->state] = prev_mddpwh_state_machines_s;
		if (app->state != MDDP_STATE_UNINIT &&
		    app->state != MDDP_STATE_WAIT_DRV_REG) {
			app->state = MDDP_STATE_WAIT_ENABLE;
			mddp_sm_on_event(app, MDDP_EVT_FUNC_ENABLE);
		}
#endif
		if (app->drv_hdlr.wifi_handle != NULL) {
			struct mddpw_drv_handle_t *wifi_handle =
				app->drv_hdlr.wifi_handle;
			if (wifi_handle->notify_md_info != NULL) {
				md_info.version = 0;
				md_info.info_type = 1;
				md_info.buf_len = 0;
				wifi_handle->notify_md_info(&md_info);
			}
		}
		mddpw_reset_ongoing = 0;
	} else {
		timer = 100;
		pr_info("%s: timer start (%d).\n", __func__, timer);
		mod_timer(&mddpw_timer, jiffies + msecs_to_jiffies(timer));
	}
}

void mddpw_reset_work(unsigned long data)
{
	schedule_work(&(mddpw_reset_workq));
}

int32_t mddpw_wfpm_msg_hdlr(uint32_t msg_id, void *buf, uint32_t buf_len)
{
	struct mddp_app_t                      *app;
	struct mddp_ilm_common_rsp_t           *rsp;
	struct wfpm_enable_md_func_rsp_t       *enable_rsp;
	struct mddpw_md_notify_info_t          *md_info;

	// NG. The length of rx_msg is incorrect!
	if (!mddp_ipc_rx_msg_validation(msg_id, buf_len))
		return -EINVAL;

	rsp = (struct mddp_ilm_common_rsp_t *) buf;
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	switch (msg_id) {
	case IPC_MSG_ID_WFPM_ENABLE_MD_FAST_PATH_RSP:
		enable_rsp = (struct wfpm_enable_md_func_rsp_t *) buf;
		pr_info("%s: set (%u), (%u), MD version(%u), (%u).\n",
		__func__, enable_rsp->mode, enable_rsp->result,
		enable_rsp->version, enable_rsp->reserved);
		mddp_set_md_version(enable_rsp->version);

		if (rsp->rsp.result) {
			/* ENABLE OK. */
			pr_info("%s: ENABLE RSP OK, result(%d).\n",
					__func__, rsp->rsp.result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_ENABLING, true);
		} else {
			/* ENABLE FAIL. */
			pr_notice("%s: ENABLE RSP FAIL, result(%d)!\n",
					__func__, rsp->rsp.result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_ENABLING, false);
		}
		break;

	case IPC_MSG_ID_WFPM_DISABLE_MD_FAST_PATH_RSP:
		if (rsp->rsp.result) {
			/* DISABLE OK. */
			pr_info("%s: DISABLE RSP OK, result(%d).\n",
					__func__, rsp->rsp.result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_DISABLING, true);
		} else {
			/* DISABLE FAIL. */
			pr_notice("%s: DISABLE RSP FAIL, result(%d)!\n",
					__func__, rsp->rsp.result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_DISABLING, false);
		}
		break;

	case IPC_MSG_ID_WFPM_ACTIVATE_MD_FAST_PATH_RSP:
		if (rsp->rsp.result) {
			/* ACT OK. */
			pr_info("%s: ACT RSP OK, result(%d).\n",
					__func__, rsp->rsp.result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_ACTIVATING, true);
		} else {
			/* ACT FAIL. */
			pr_notice("%s: ACT RSP FAIL, result(%d)!\n",
					__func__, rsp->rsp.result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_ACTIVATING, false);
		}
		break;

	case IPC_MSG_ID_WFPM_DEACTIVATE_MD_FAST_PATH_RSP:
		if (rsp->rsp.result) {
			/* DEACT OK. */
			pr_info("%s: DEACT RSP OK, result(%d)\n",
					__func__, rsp->rsp.result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_DEACTIVATING, true);

			memcpy(&deact_rsp_metadata_s,
					buf,
					sizeof(deact_rsp_metadata_s));
		} else {
			/* DEACT FAIL. */
			pr_notice("%s: DEACT RSP FAIL, result(%d)\n",
					__func__, rsp->rsp.result);
			mddp_sm_set_state_by_md_rsp(app,
				MDDP_STATE_DEACTIVATING, false);

			memcpy(&deact_rsp_metadata_s,
					buf,
					sizeof(deact_rsp_metadata_s));
		}
		break;

	case IPC_MSG_ID_WFPM_RESET_IND:
		pr_notice("%s: Received WFPM RESET IND\n", __func__);
		if (mddpw_reset_ongoing == 0) {
			mddpw_reset_ongoing = 1;
#ifdef CONFIG_MTK_MDDP_WH_SUPPORT
			prev_mddpwh_state_machines_s =
				app->state_machines[app->state];
			app->state_machines[app->state] =
				mddpwh_dead_state_machine_s;
#endif
			mddpw_timer.function = mddpw_reset_work;
			mod_timer(&mddpw_timer,
					jiffies + msecs_to_jiffies(100));
		} else
			pr_notice("%s: WFPM RESET ongoing", __func__);
		break;
	case IPC_MSG_ID_WFPM_MD_NOTIFY:
		pr_notice("%s: Received WFPM MD NOTIFY\n", __func__);
		md_info = (struct mddpw_md_notify_info_t *) buf;
		if (app->drv_hdlr.wifi_handle != NULL)
			if (app->drv_hdlr.wifi_handle->notify_md_info) {
				pr_notice("%s: MD NOTIFY info_type[%d] len[%d]\n",
					__func__, md_info->info_type,
					md_info->buf_len);
				app->drv_hdlr.wifi_handle->notify_md_info(
						md_info);
			}
		break;

	default:
		pr_notice("%s: Unsupported RSP MSG_ID[%d] from WFPM.\n",
					__func__, msg_id);
		break;
	}

	return 0;
}

int32_t mddpw_drv_add_txd(struct mddpw_txd_t *txd)
{
	struct mddp_md_msg_t    *md_msg;
	struct mddp_app_t       *app;

	// Send TXD to MD
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config) {
		pr_notice("%s: app_type(MDDP_APP_TYPE_WH) not configured!\n",
		__func__);
		return -ENODEV;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t) +
	sizeof(struct mddpw_txd_t) + txd->txd_length, GFP_KERNEL);

	if (unlikely(!md_msg)) {
		WARN_ON(1);
		return -ENOMEM;
	}

	md_msg->msg_id = IPC_MSG_ID_WFPM_SEND_MD_TXD_NOTIFY;
	md_msg->data_len = sizeof(struct mddpw_txd_t) + txd->txd_length;
	memcpy(md_msg->data, txd, md_msg->data_len);
	mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);

	return 0;
}

int32_t mddpw_drv_get_net_stat(struct mddpw_net_stat_t *usage)
{
	// Use global variable to cache previous statistics,
	// and return delta value each call.

	static struct mddpw_net_stat_t     cur_stats = {0};
	struct mddpw_net_stat_t           *md_stats;
	uint8_t                             smem_attr;
	uint32_t                            smem_size;

	if (!usage) {
		pr_notice("%s: usage is NULL!\n", __func__);
		return -EINVAL;
	}
	memset(usage, 0, sizeof(struct mddpw_net_stat_t));

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_WIFI_STATISTICS,
				(void **)&md_stats, &smem_attr, &smem_size)) {
		pr_notice("%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_WIFI_STATISTICS);
		return -EFAULT;
	}

	if (md_stats && smem_size > 0) {
		#define DIFF_FROM_SMEM(x) (usage->x = \
		(md_stats->x > cur_stats.x) ? (md_stats->x - cur_stats.x) : 0)
		DIFF_FROM_SMEM(tx_packets);
		DIFF_FROM_SMEM(rx_packets);
		DIFF_FROM_SMEM(tx_bytes);
		DIFF_FROM_SMEM(rx_bytes);
		DIFF_FROM_SMEM(tx_errors);
		DIFF_FROM_SMEM(rx_errors);

		memcpy(&cur_stats, md_stats, sizeof(struct mddpw_net_stat_t));
	}

	return 0;
}

int32_t mddpw_drv_get_ap_rx_reorder_buf(
	struct mddpw_ap_reorder_sync_table_t **ap_table)
{
	uint8_t      smem_attr;
	uint32_t     smem_size;

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_RX_REORDER_TO_MD,
				(void **)ap_table, &smem_attr, &smem_size)) {
		pr_notice("%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_RX_REORDER_TO_MD);
		return -EINVAL;
	}

	return 0;
}

int32_t mddpw_drv_get_md_rx_reorder_buf(
	struct mddpw_md_reorder_sync_table_t **md_table)
{
	uint8_t      smem_attr;
	uint32_t     smem_size;

	if (mddp_ipc_get_md_smem_by_id(MDDP_MD_SMEM_USER_RX_REORDER_FROM_MD,
				(void **)md_table, &smem_attr, &smem_size)) {
		pr_notice("%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_RX_REORDER_FROM_MD);
		return -EINVAL;
	}

	return 0;
}

int32_t mddpw_drv_notify_info(
	struct mddpw_drv_notify_info_t *wifi_notify)
{
	struct mddp_md_msg_t    *md_msg;
	struct mddp_app_t       *app;

	// Send WIFI Notify to MD
	app = mddp_get_app_inst(MDDP_APP_TYPE_WH);

	if (!app->is_config) {
		pr_notice("%s: app_type(MDDP_APP_TYPE_WH) not configured!\n",
		__func__);
		return -ENODEV;
	}

	md_msg = kzalloc(sizeof(struct mddp_md_msg_t) +
		sizeof(struct mddpw_drv_notify_info_t) +
		wifi_notify->buf_len, GFP_KERNEL);

	if (unlikely(!md_msg)) {
		WARN_ON(1);
		return -ENOMEM;
	}

	md_msg->msg_id = IPC_MSG_ID_WFPM_DRV_NOTIFY;
	md_msg->data_len = sizeof(struct mddpw_drv_notify_info_t) +
		wifi_notify->buf_len;
	memcpy(md_msg->data, wifi_notify, md_msg->data_len);
	mddp_ipc_send_md(app, md_msg, MDFPM_USER_ID_NULL);

	return 0;
}

int32_t mddpw_drv_reg_callback(struct mddp_drv_handle_t *handle)
{
	struct mddpw_drv_handle_t         *wifi_handle;

	if (handle->wifi_handle == NULL) {
		pr_notice("%s: handle NULL\n",
		__func__);
		return -EINVAL;
	}

	wifi_handle = handle->wifi_handle;

	wifi_handle->add_txd = mddpw_drv_add_txd;
	wifi_handle->get_net_stat = mddpw_drv_get_net_stat;
	wifi_handle->get_ap_rx_reorder_buf = mddpw_drv_get_ap_rx_reorder_buf;
	wifi_handle->get_md_rx_reorder_buf = mddpw_drv_get_md_rx_reorder_buf;
	wifi_handle->notify_drv_info = mddpw_drv_notify_info;

	return 0;
}

int32_t mddpw_drv_dereg_callback(struct mddp_drv_handle_t *handle)
{
	struct mddpw_drv_handle_t         *wifi_handle;

	if (handle->wifi_handle == NULL) {
		pr_notice("%s: handle NULL\n",
		__func__);
		return -EINVAL;
	}

	wifi_handle = handle->wifi_handle;

	wifi_handle->add_txd = NULL;
	wifi_handle->get_net_stat = NULL;
	wifi_handle->get_ap_rx_reorder_buf = NULL;
	wifi_handle->get_md_rx_reorder_buf = NULL;
	wifi_handle->notify_drv_info = NULL;

	return 0;
}

ssize_t mddpwh_sysfs_callback(
	struct mddp_app_t *app,
	enum mddp_sysfs_cmd_e cmd,
	char *buf,
	size_t buf_len)
{
	static uint8_t                  mddpwh_state = 1;
	struct mddpw_net_stat_t        *md_stats;
	uint8_t                         smem_attr;
	uint32_t                        smem_size;
	uint32_t                        show_cnt = 0;

	if (cmd == MDDP_SYSFS_CMD_ENABLE_WRITE) {
		if (sysfs_streq(buf, "1")) {
			app->state_machines[MDDP_STATE_DEACTIVATED] =
				mddpwh_deactivated_state_machine_s;
			mddpwh_state = 1;
			pr_notice("%s: enable!\n", __func__);
		} else if (sysfs_streq(buf, "0")) {
			app->state_machines[MDDP_STATE_DEACTIVATED] =
				mddpwh_dead_state_machine_s;
			mddpwh_state = 0;
			pr_notice("%s: disable!\n", __func__);
		} else
			buf_len = 0;
		return buf_len;
	} else if (cmd == MDDP_SYSFS_CMD_ENABLE_READ)
		return sprintf(buf, "wh_enable(%d)\n", mddpwh_state);
	else if (cmd == MDDP_SYSFS_CMD_STATISTIC_READ) {
		if (mddp_ipc_get_md_smem_by_id(
				MDDP_MD_SMEM_USER_WIFI_STATISTICS,
				(void **)&md_stats, &smem_attr, &smem_size)) {
			pr_notice("%s: Failed to get smem_id (%d)!\n",
				__func__, MDDP_MD_SMEM_USER_WIFI_STATISTICS);
			return -EINVAL;
		}

		show_cnt += sprintf(buf, "\n[MDDP-WH State]\n%d\n",
				mddpwh_state);
		show_cnt += sprintf(buf + show_cnt, "[MDDP-WH Statistics]\n");
		show_cnt += sprintf(buf + show_cnt,
			"%s\t\t%s\t\t%s\t%s\t%s\t%s\n",
			"tx_pkts", "rx_pkts",
			"tx_bytes", "rx_bytes",
			"tx_error", "rx_error");
		show_cnt += sprintf(buf + show_cnt,
			"%lld\t\t%lld\t\t%lld\t\t%lld\t\t%lld\t\t%lld\n",
			md_stats->tx_packets, md_stats->rx_packets,
			md_stats->tx_bytes, md_stats->rx_bytes,
			md_stats->tx_errors, md_stats->rx_errors);
		return show_cnt;
	} else
		return 0;
}

int32_t mddpwh_sm_init(struct mddp_app_t *app)
{
	memcpy(&app->state_machines,
		&mddpwh_state_machines_s,
		sizeof(mddpwh_state_machines_s));

	pr_info("%s: %p, %p\n",
		__func__, &(app->state_machines), &mddpwh_state_machines_s);
	mddp_dump_sm_table(app);

	app->md_recv_msg_hdlr = mddpw_wfpm_msg_hdlr;
	app->reg_drv_callback = mddpw_drv_reg_callback;
	app->dereg_drv_callback = mddpw_drv_dereg_callback;
	app->sysfs_callback = mddpwh_sysfs_callback;
	memcpy(&app->md_cfg, &mddpw_md_cfg_s, sizeof(struct mddp_md_cfg_t));
	app->is_config = 1;

	init_timer(&mddpw_timer);
	INIT_WORK(&(mddpw_reset_workq), mddpw_ack_md_reset);
	return 0;
}
