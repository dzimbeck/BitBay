#sudo apt-get update
#sudo apt-get install qt5-default qt5-qmake qtbase5-dev-tools qttools5-dev-tools
cd src/leveldb
chmod 755 build_detect_platform
make out-static/libmemenv.a
make out-static/libleveldb.a
cd ..
cd ..
qmake
make
