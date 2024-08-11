# 3장: I/O 스케쥴러의 변경

## cgroup v1

슈퍼 유저 권한이 필요하므로 슈퍼 유저로 로그인합니다.
본 챕터에서는 cgroup v1 기능을 활용해서 실행시키도록 합니다.

먼저 제가 사용하는 스토리지의 지원하는 I/O 스케쥴러가 어떤 것이 있는지 확인하도록 합니다.

> 주의 사항: NVMe가 아니기때문 비율 배분이 정확하게 이루어지지 않을 수 있음을 유의하고 챕터를 읽으셔야 합니다.

```bash
sudo -s
# 저는 sda 디스크의 sda2를 사용하겠습니다.
cat /sys/block/sda/queue/scheduler
```

만약에 `bfq-iosched`랑 `kyber`가 없다면 다음 명령어를 통해서 추가해주도록 합니다.

```bash
modprobe bfq-iosched
modprobe kyber-iosched
cat /sys/block/sda/queue/scheduler # bfq랑 kyber가 추가되었음을 알 수 있다.
```

`bfq`로 I/O 스케쥴러를 변경해주도록 합니다.

```bash
echo "bfq" > /sys/block/sda/queue/scheduler
cat /sys/block/sda/queue/scheduler # bfq
```

이 다음에 cgroup에서 테스트를 위한 디렉터리를 2개를 만들어줍니다.

```bash
mkdir /sys/fs/cgroup/blkio/fio1
mkdir /sys/fs/cgroup/blkio/fio2
echo "50" > /sys/fs/cgroup/blkio/fio1/blkio.bfq.weight
echo "100" > /sys/fs/cgroup/blkio/fio1/blkio.bfq.weight
```

> 만약에 blkio.bfq.weight가 없는 경우에는 `/etc/default/grub` 파일을 열어서
> `GRUB_CMDLINE_LINX` 항목에 `cgroup_enable=memory systemd.unified_cgroup_hierarchy=0`를 추가해주시면 됩니다.
> 그리고 완료되면 `sudo update-grub`을 진행해주시면 됩니다. 그 후에 reboot을 수행해주시면 blkio.bfq.weight 항목이 생기는 것을 확인할 수 있습니다.

2개의 서로 다른 bash 창을 열어주셔야 합니다. 저는 tmux를 사용했습니다. ([참고](https://hamvocke.com/blog/a-quick-and-easy-guide-to-tmux/))

그리고 각각 bash에서 다음 명령어를 실행해주도록 합니다.

```bash
# 첫 번째 배시 창에서 실행을 해주도록 합니다.
echo $$ > /sys/fs/cgroup/blkio/fio1/cgroup.procs
sudo fio --name=randomwrite \
    --ioengine=libaio \
    --iodepth=16 \
    --rw=randwrite \
    --size=2G \
    --filename=${HOME}/data1

# 두번째 배시 창에서 실행을 해주도록 합니다.
echo $$ > /sys/fs/cgroup/blkio/fio2/cgroup.procs
sudo fio --name=randomwrite \
    --ioengine=libaio \
    --iodepth=16 \
    --rw=randwrite \
    --size=2G \
    --filename=${HOME}/data2
```

그러면 weight가 더 높은 쪽에서 더 많은 대역폭을 대체로 가져가는 것을 확인할 수 있습니다.
그리고 `/sys/fs/cgroup/blkio/fio1` 또는 `fio2`에 있는 파일들을 `cat` 명령어로 확인하여 어떻게 할당되는지를 알 수 있습니다.

## cgroup v2

향후에는 cgroup v2의 `CONFIG_BLK_CGROUP_IOCOST`를 커널에서 활성화해준 후에 weight 값을 할당해서 bio에서 weight 값을 가져오는 것을
실습해보면 좋을 것 같습니다. ([cgroup v2 참고 링크](https://docs.kernel.org/admin-guide/cgroup-v2.html))

## 참고 자료

- https://www.cnx-software.com/2019/08/14/bfq-budget-fair-queuing-i-o-scheduler-improves-linux-systems-responsiveness-video/
