#!/usr/bin/env bash

#exportA
function Export {
  export ROOT_DIR=${PWD}
  export TOOLS_DIR=${ROOT_DIR}/tools
  export CHROMIUM_DIR=${ROOT_DIR}/src
  export SYSROOT_DIR=${ROOT_DIR}/sysroot
  export TARGET_DIR=${ROOT_DIR}/target/pclinux
  export OUT_DIR=${ROOT_DIR}/out_pclinux
}

#prepare
function Prepare {
  if [ ! -d ${OUT_DIR} ]
  then
    mkdir -p ${OUT_DIR}
  fi
  cp -f ${TARGET_DIR}/args.gn ${OUT_DIR}/args.gn
  tar -zxvf ${SYSROOT_DIR}/debian_sid_amd64-sysroot.tgz -C ${SYSROOT_DIR} > /dev/null
}

#build
function Build {
  cd ${CHROMIUM_DIR}
  ${TOOLS_DIR}/gn gen ${OUT_DIR}
  CheckResult
  echo -e "\033[36m[easy] build content_shell... \033[0m"
  ${TOOLS_DIR}/ninja -C ${OUT_DIR} content_shell
  CheckResult
  echo -e "\033[36m[easy] build chromium... \033[0m"
  ${TOOLS_DIR}/ninja -C ${OUT_DIR} chrome
  CheckResult
  cd ${ROOT_DIR}
}

#delete generate files
function Delete {
  find -name "*.pyc" -exec rm -f {} \;
}

#check result
function CheckResult {
  if [ $? != 0 ]
  then
    echo -e "\033[31;5m[easy] build failed \033[0m"
    Delete
    exit 1
  fi
}

Export
Prepare
Build
Delete

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo -e "\033[32;5m[easy] Congratulations! Build successful at ${OUT_DIR} \033[0m "
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
