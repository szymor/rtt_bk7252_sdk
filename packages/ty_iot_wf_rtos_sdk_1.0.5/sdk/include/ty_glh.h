/**
 * @file ty_glh.h
 * @brief 
 * @date 2019-12-08
 * 
 * copyright Copyright (c) {2018-2020} 涂鸦科技 www.tuya.com
 * 
 */
#ifndef _TY_GLH_H_
#define _TY_GLH_H_
#ifdef __cplusplus
extern "C" {
#endif
#include "tuya_cloud_types.h"
#include "cJSON.h"
OPERATE_RET tuya_svc_glh_start(VOID);
OPERATE_RET tuya_svc_glh_active_json_handle(IN CONST ty_cJSON *result);

#ifdef __cplusplus
}
#endif/* __cplusplus */
#endif/* _TY_GLH_H_ */
