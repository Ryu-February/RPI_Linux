# 캐릭터 디바이스 — 완료

sysfs 인터페이스는 그대로 두고 `/dev/my_device` 를 **나란히 추가**했습니다. 같은 GPIO를 두 경로로 제어하면서 두 방식의 차이를 직접 비교하는 것이 목적이었습니다.

**등록 3단계**

| 단계 | 함수 | 하는 일 |
| --- | --- | --- |
| 1 | `alloc_chrdev_region()` | 장치 번호(major/minor)를 커널에 요청 |
| 2 | `cdev_add()` | 그 번호에 `file_operations` 연결 |
| 3 | `class_create()` + `device_create()` | `/dev/my_device` 파일 자동 생성 |

major를 코드에 하드코딩하는 `register_chrdev()` 대신 `alloc_chrdev_region()` 으로 커널에게 할당받아 번호 충돌을 피했습니다.

**유저 공간 메모리 접근**

```c
if (copy_from_user(tmp, buf, len))
	return -EFAULT;
```

`buf` 는 `__user` 로 표시된 유저 공간 주소입니다. 유효하지 않거나 악의적으로 커널 주소를 넘겼을 수 있어 커널이 직접 역참조하면 안 됩니다. 읽기에는 `simple_read_from_buffer()` 를 써서 파일 위치(`*off`)까지 처리했습니다. 이걸 빼면 `cat` 이 EOF를 못 만나 무한 반복합니다.

**정리는 등록의 역순**

```
등록: region → cdev → class → device
해제: device → class → cdev → region
```

실패 지점마다 그 앞 단계까지만 되감도록 계단식 `goto` 라벨을 뒀습니다. 실패한 단계 자신은 건너뛰어야 합니다 — 예를 들어 `class_create()` 가 실패했는데 `err_class` 로 가면 생성되지도 않은 클래스를 `class_destroy()` 하게 됩니다.

**검증 로그**

```
my_dev_drv my_device: GPIO acquire success
my_dev_drv my_device: chardev ready: /dev/my_device (major=236 minor=0)
my_dev_drv my_device: sysfs ready: /sys/devices/platform/my_device/value
```

```bash
$ ls -l /dev/my_device
crw------- 1 root root 236, 0 Aug 25 22:57 /dev/my_device

$ sudo cat /dev/my_device
42

$ echo 1 | sudo tee /dev/my_device   # LED 점등
```

```
chan_drv: open
chan_drv: write 1
chan_drv: release
```

`open` / `release` 가 남는 것이 sysfs와의 결정적 차이입니다. sysfs는 접근할 때마다 `show`/`store` 만 호출될 뿐 "열려 있는 상태"라는 개념이 없습니다.

`insmod` 직후 `dmesg` 출력. probe 로그와 `chardev ready` 줄이 보이도록

<img width="852" alt="insmod 직후 dmesg 출력" src="https://github.com/user-attachments/assets/26176b14-2dc8-401b-94e2-3d5e96a5e950" />

`ls -l /dev/my_device` 와 `cat /dev/my_device` 를 연달아 실행한 화면

<img width="509" alt="ls -l /dev/my_device 와 cat /dev/my_device 를 연달아 실행한 화면" src="https://github.com/user-attachments/assets/0d0297e1-9b7c-4e7a-9b88-90ddc2d0bd6e" />

`rmmod` 전후로 `ls /sys/devices/platform/my_device/` 를 비교한 화면 (`permission denied` 사진)

<img width="491" alt="rmmod 전후로 ls /sys/devices/platform/my_device/ 를 비교한 화면 (perm" src="https://github.com/user-attachments/assets/bfe1cadb-1eeb-4a79-8b0f-8b9e04dd3654" />

**언제 무엇을 쓰는가**

| sysfs | 캐릭터 디바이스 |
| --- | --- |
| 설정값 몇 개를 읽고 쓰기 | 데이터를 스트림으로 주고받기 |
| 값 하나당 파일 하나, 텍스트 | 바이너리 가능, 큰 데이터 |
| 상태 없음 | open~close 사이 상태 유지 |
| `cat`/`echo` 로 바로 확인 | `ioctl` 로 명령 전달 가능 |

연결이 유지되는 장치(시리얼·카메라·오디오)는 `open` 에서 하드웨어를 깨우고 `release` 에서 재우는 구조가 필요하므로 캐릭터 디바이스여야 합니다.

---

[← 4. 디바이스 트리 오버레이 전환](04-device-tree-overlay.md) · [목차](../README.md) · [6. LED 서브시스템 →](06-led-subsystem.md)
