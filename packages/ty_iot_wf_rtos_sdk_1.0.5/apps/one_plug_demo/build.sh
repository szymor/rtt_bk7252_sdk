#!/bin/sh

APP_BIN_NAME=$1
USER_SW_VER=$2
TARGET_PLATFORM=$3
echo APP_BIN_NAME=$APP_BIN_NAME
echo USER_SW_VER=$USER_SW_VER
echo TARGET_PLATFORM=$TARGET_PLATFORM


[ -z $APP_BIN_NAME ] && echo "no app name!" && exit 99
[ -z $USER_SW_VER ] && echo "no version!" && exit 99
[ -z $TARGET_PLATFORM ] && echo "no platform!" && exit 99


set -e

#cd `dirname $0`
echo dirname $0
export APP_BIN_NAME USER_SW_VER
cd ../../platforms/$TARGET_PLATFORM/project/realtek_amebaz_va0_example/GCC-RELEASE
sh build_app_2M.sh $APP_BIN_NAME $USER_SW_VER $TARGET_PLATFORM

cd -
if [ -z $CI_PACKAGE_PATH ]; then
   exit
else
    mkdir -p ${CI_PACKAGE_PATH}

    cp ./output/${USER_SW_VER}/$APP_BIN_NAME"_ug_"$USER_SW_VER.bin ${CI_PACKAGE_PATH}/$APP_BIN_NAME"_UG_"$USER_SW_VER.bin
    cp ./output/${USER_SW_VER}/$APP_BIN_NAME"_ug_"$USER_SW_VER.bin ${CI_PACKAGE_PATH}/$APP_BIN_NAME"_UA_"$USER_SW_VER.bin
    cp ./output/${USER_SW_VER}/$APP_BIN_NAME"_QIO_"$USER_SW_VER.bin ${CI_PACKAGE_PATH}
    cp ./output/${USER_SW_VER}/$APP_BIN_NAME"_DOUT_"$USER_SW_VER.bin ${CI_PACKAGE_PATH}
	chmod 777 ./output/${USER_SW_VER}/ -R
fi

