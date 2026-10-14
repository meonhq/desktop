#!/bin/bash
# Windows 배포: build/ 의 Release 결과물을 MEON\ 에 모으고 Inno Setup 으로 설치 프로그램을 만든다.
# 사용법 (release/ 에서, Git Bash): ./distwin.sh <version>
# 결과: release/instoutput/meon-<version>-setup.exe
# 코드 서명은 하지 않는다 (인증서 없음) — 처음 실행 시 SmartScreen 경고가 뜬다.

set -e

if [ -z "$1" ] ; then
   echo "Usage: $0 <version>"
   exit 1
fi

VERSION=$1
BUILDDIR='../build/MEON_artefacts/Release'

rm -rf MEON
mkdir -p MEON/Plugins/VST3

cp -v ../doc/README_WINDOWS.txt MEON/README.txt
cp -v ${BUILDDIR}/Standalone/Meon.exe MEON/
cp -pRv ${BUILDDIR}/VST3/Meon.vst3 MEON/Plugins/VST3/
# VST3 폴더 아이콘 (VST3 규격): 번들 폴더에 PlugIn.ico 와 desktop.ini
cp -v ../images/icons/MEON.ico MEON/Plugins/VST3/Meon.vst3/PlugIn.ico
printf '[.ShellClassInfo]\r\nIconResource=PlugIn.ico,0\r\n' > MEON/Plugins/VST3/Meon.vst3/desktop.ini

mkdir -p instoutput
rm -f instoutput/*

ISCC=${ISCC:-iscc}
"$ISCC" //O"instoutput" //DSBVERSION="${VERSION}" wininstaller.iss
