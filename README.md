# ZhongYuan基于CNStream项目

## 编译方式
```bash
cp rtsp_handler.patch CNStream
cd CNStream
git apply rtsp_handler.patch
cd ..
mkdir build && cd build
cmake ..
make -j4
cd ..
```

## 运行方式
```bash
./run.sh
```