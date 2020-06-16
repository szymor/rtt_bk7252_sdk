#!/bin/sh

APP_PATH=$1
APP_NAME=$2
APP_VERSION=$3
echo APP_PATH=$APP_PATH
echo APP_NAME=$APP_NAME
echo APP_VERSION=$APP_VERSION


fatal() {
    echo -e "\033[0;31merror: $1\033[0m"
    exit 1
}


[ -z $APP_PATH ] && fatal "no app path!"
[ -z $APP_NAME ] && fatal "no app name!"
[ -z $APP_VERSION ] && fatal "no version!"


DEBUG_FLAG=`echo $APP_VERSION | sed -n 's,^[0-9]\+\.\([0-9]\+\)\.[0-9]\+\.*$,\1,p'`
if [ $((DEBUG_FLAG%2))=0 ]; then
    export APP_DEBUG=1
fi


cd `dirname $0`

TARGET_PLATFORM=rtl8710bn
TARGET_PLATFORM_REPO=https://registry.code.tuya-inc.top/hardware_wifi_soc/rtl8710bn.git
TARGET_PLATFORM_VERSION=1.0.17
ROOT_DIR=$(pwd)

# 下载编译环境
if [ ! -d platforms/$TARGET_PLATFORM ]; then
    if [ -n "$TARGET_PLATFORM_REPO" ]; then
        # checkout toolchain
        git clone $TARGET_PLATFORM_REPO platforms/$TARGET_PLATFORM
    fi
fi

# 切换到指定版本
if [ -e platforms/$TARGET_PLATFORM/.git ]; then
    cd platforms/$TARGET_PLATFORM
    git checkout -- .
    git fetch
    if [ -n "$TARGET_PLATFORM_VERSION" ]; then
        git checkout $TARGET_PLATFORM_VERSION
    else
        git checkout master
    fi
    cd -
fi

if [ -f platforms/$TARGET_PLATFORM/prepare.sh ]; then
    bash platforms/$TARGET_PLATFORM/prepare.sh
fi

# 判断当前编译环境是否OK
PLATFORM_BUILD_PATH_FILE=${ROOT_DIR}/platforms/$TARGET_PLATFORM/toolchain/build_path
if [ -e $PLATFORM_BUILD_PATH_FILE ]; then
    . $PLATFORM_BUILD_PATH_FILE
    if [ -n "$TUYA_SDK_TOOLCHAIN_ZIP" ];then
        if [ ! -f ${ROOT_DIR}/platforms/${TARGET_PLATFORM}/toolchain/${TUYA_SDK_BUILD_PATH}gcc ]; then
            echo "unzip file $TUYA_SDK_TOOLCHAIN_ZIP"
            tar -xf ${ROOT_DIR}/platforms/$TARGET_PLATFORM/toolchain/$TUYA_SDK_TOOLCHAIN_ZIP -C ${ROOT_DIR}/platforms/$TARGET_PLATFORM/toolchain/
            echo "unzip finish"
        fi
    fi
else
    echo "$PLATFORM_BUILD_PATH_FILE not found in platform[$TARGET_PLATFORM]!"
fi

if [ -z "$TUYA_SDK_BUILD_PATH" ]; then
    COMPILE_PREX=
else
    COMPILE_PREX=${ROOT_DIR}/platforms/$TARGET_PLATFORM/toolchain/$TUYA_SDK_BUILD_PATH
fi

cd $APP_PATH
if [ -f build.sh ]; then
    sh ./build.sh $APP_NAME $APP_VERSION $TARGET_PLATFORM
else
    export COMPILE_PREX TARGET_PLATFORM
    make APP_BIN_NAME=$APP_NAME USER_SW_VER=$APP_VERSION all
fi

