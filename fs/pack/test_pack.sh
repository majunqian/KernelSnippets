#!/bin/bash
set -e

MODULE=pack.ko
MODPARAM=cmd
TESTDIR=/tmp/pack_test_dir
TARFILE=/tmp/pack_test.tar
OUTDIR=/tmp/pack_test_out

# 清理环境
echo "[TEST] 清理测试环境..."
rm -rf $TESTDIR $TARFILE $OUTDIR
mkdir -p $TESTDIR

echo "[TEST] 创建测试文件..."
echo "hello kernel" > $TESTDIR/file1.txt
mkdir $TESTDIR/subdir
cp $TESTDIR/file1.txt $TESTDIR/subdir/file2.txt

# 设置权限和属主属组
sudo chmod 600 $TESTDIR/file1.txt
sudo chmod 700 $TESTDIR/subdir
sudo chmod 644 $TESTDIR/subdir/file2.txt
sudo chown root:root $TESTDIR/file1.txt
sudo chown nobody:nobody $TESTDIR/subdir
sudo chown nobody:nobody $TESTDIR/subdir/file2.txt

# 编译并加载模块进行打包
echo "[TEST] 编译并加载模块进行打包..."
make clean && make
sudo insmod $MODULE $MODPARAM=\"pack $TESTDIR $TARFILE\"
sudo rmmod pack

# 检查tar文件
echo "[TEST] 检查tar文件..."
ls -l $TARFILE

# 加载模块进行解包
echo "[TEST] 加载模块进行解包..."
sudo insmod $MODULE $MODPARAM=\"unpack $TARFILE $OUTDIR\"
sudo rmmod pack

# 对比原始和解包内容
echo "[TEST] 对比原始和解包内容..."
diff -r $TESTDIR $OUTDIR/$TESTDIR
# 对比权限、属主、属组
find $TESTDIR -exec stat -c "%n %a %U %G" {} \; | sed "s|$TESTDIR/||;s|$TESTDIR||" | sort > /tmp/orig_meta.txt
find $OUTDIR/$TESTDIR -exec stat -c "%n %a %U %G" {} \; | sed "s|$OUTDIR/$TESTDIR/||;s|$OUTDIR/$TESTDIR||" | sort > /tmp/unpack_meta.txt
diff /tmp/orig_meta.txt /tmp/unpack_meta.txt
if [ $? -eq 0 ]; then
    echo "[TEST] 打包/解包自测通过！"
else
    echo "[TEST] 打包/解包自测失败！"
    exit 1
fi
