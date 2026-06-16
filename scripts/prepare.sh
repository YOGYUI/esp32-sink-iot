#! /usr/sh

cur_path=${PWD}
if [[ "$OSTYPE" == "darwin"* ]]; then
    project_path=$(dirname $(dirname $(realpath $0)))
else 
    project_path=$(dirname $(dirname $(realpath $BASH_SOURCE)))
fi

sdk_path=~/tools/esp-idf  # change to your own sdk path
source ${sdk_path}/export.sh

echo "------------------------------------------------------"
echo "[esp-idf]"
cd ${sdk_path}
git rev-parse HEAD
git describe --tags
echo "------------------------------------------------------"

cd ${cur_path}