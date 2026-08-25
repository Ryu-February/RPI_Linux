# RPi4 Linux Device Driver Practice

라즈베리파이 4B(64bit)에서 커널을 직접 크로스 컴파일하고, 디바이스 트리부터 캐릭터 디바이스까지 리눅스 디바이스 드라이버를 단계적으로 구현한 **개인 학습 프로젝트**입니다.

> 실무 프로젝트가 아닌 학습 목적의 개인 프로젝트입니다. 모든 코드는 직접 작성했고, 모든 동작은 실제 RPi4 보드에서 검증했습니다.

---

## 이 프로젝트에서 다룬 것

- 커널 소스 크로스 컴파일 (x86 WSL2 → aarch64) 및 타깃 보드 커널 교체
- 디바이스 트리(DTS) 노드 선언과 플랫폼 드라이버 `compatible` 매칭
- `probe()` / `remove()` 생명주기와 `devm_` 리소스 관리
- sysfs 인터페이스(`show`/`store`)를 통한 유저 공간 제어
- GPIO 서브시스템(`gpiod`) 으로 실제 핀 제어 및 LED 점등
- 보드 DTS 직접 수정 → **디바이스 트리 오버레이** 방식으로 전환
- 캐릭터 디바이스(`/dev`) 인터페이스 — `file_operations`, `copy_from_user()`, 계단식 에러 처리

---

## 개발 환경

| 항목 | 값 |
| --- | --- |
| 타깃 보드 | Raspberry Pi 4 Model B (64bit) |
| 커널 | `6.12.28-v8+` (rpi-6.12.y) |
| 빌드 환경 | WSL2 Ubuntu 24.04 (x86_64) |
| 툴체인 | `aarch64-linux-gnu-` |
| 빌드 설정 | `ARCH=arm64`, `bcm2711_defconfig`, `Image` |
| 배포 | `scp` → `insmod` (재부팅 없이 반복 개발) |

빌드는 커널 소스 트리 밖에서 `O=out` 으로 분리했고, 외부 모듈은 `M=$(CURDIR)` 로 개별 빌드합니다.

---

## 진행 단계

각 단계는 **동작 검증까지 완료된 것만** 완료로 표기했습니다.

### 1. 커널 크로스 컴파일 및 교체 — 완료

x86 WSL2에서 aarch64 커널을 빌드해 RPi4의 순정 커널을 교체했습니다.
`kernel8.img` + `*.dtb` + `/lib/modules/<버전>` 3종 세트를 모두 이식해야 부팅된다는 점을 실패를 통해 확인했습니다.

→ 상세: [TROUBLESHOOTING.md](TROUBLESHOOTING.md#1-커널-이미지-교체-후-부팅-불가)

### 2. 플랫폼 드라이버 + DTS 매칭 — 완료

디바이스 트리에 가상 노드를 선언하고, `of_device_id` 의 `compatible` 문자열로 드라이버를 매칭시켜 `probe()` 를 호출시켰습니다.

```c
static const struct of_device_id my_match[] = {
	{ .compatible = "chan,my-device" },
	{ }
};
```

DTS에서 `my-number`, `my-string` 프로퍼티를 읽어 드라이버가 하드웨어 정보를 **소스에 하드코딩하지 않고** 받아쓰는 구조를 구현했습니다.

### 3. sysfs + GPIO 제어 — 완료

`devm_gpiod_get()` 으로 GPIO 17번 핀을 확보하고, sysfs 파일을 통해 유저 공간에서 LED를 제어합니다.

```
DTS: my_device { gpios = <&gpio 17 0>; }
  │  부팅 시 커널이 DTB를 읽어 플랫폼 디바이스로 등록
  ▼
드라이버(chan_drv.ko): compatible 매칭 → probe() → devm_gpiod_get()
  ▼
유저: echo 1 > /sys/devices/platform/my_device/value
  ▼
gpiod_set_value() → GPIO 17 HIGH → LED 점등
```

**검증 로그**

```
my_dev_drv my_device: probe called (node=/my_device)
my_dev_drv my_device: my-number = 42
my_dev_drv my_device: my-string = hello
my_dev_drv my_device: GPIO acquire success
my_dev_drv my_device: sysfs ready: /sys/devices/platform/my_device/value
```

```bash
echo 1 | sudo tee /sys/devices/platform/my_device/value   # 점등
echo 0 | sudo tee /sys/devices/platform/my_device/value   # 소등
```

배선: GPIO 17 (물리 11번) → 330Ω → LED → GND (물리 6번)

<!-- TODO: LED 점등 사진 docs/led-on.jpg 추가 -->

### 4. 디바이스 트리 오버레이 전환 — 완료

동작은 그대로 두고 **적용 방식만** 보드 DTS 직접 수정에서 오버레이로 교체했습니다.

| | 전 | 후 |
| --- | --- | --- |
| 노드 위치 | `bcm2711-rpi-4-b.dts` 직접 수정 | `my-device-overlay.dts` 별도 파일 |
| 빌드 | `make dtbs` (수분~수십분) | `dtc` (수초) |
| 적용 | 베이스 DTB 덮어쓰기 | `.dtbo` 복사 + `config.txt` 한 줄 |
| 해제 | 소스 되돌리고 재빌드 | `config.txt` 주석 처리 |

바꾼 이유는 반복 주기 단축만이 아닙니다. 보드 DTS는 메인라인 소스라 **커널 업데이트 시 수정사항이 날아가고**, 남에게 배포할 수도 없습니다. 오버레이는 `.dtbo` 파일 하나만 건네주면 되고, 라즈베리파이가 HAT·센서를 붙이는 표준 방식이기도 합니다.

```bash
dtc -@ -I dts -O dtb -o my-device.dtbo my-device-overlay.dts
echo "dtoverlay=my-device" | sudo tee -a /boot/firmware/config.txt
```

`-@` 옵션이 핵심입니다. 심볼 정보를 포함시켜야 `&gpio` 같은 라벨이 부팅 시 실제 노드로 연결됩니다.

**결과: 반복 주기에서 커널 빌드가 사라졌습니다.** (DTS 수정 → `dtc` → `scp` → reboot)

### 5. 캐릭터 디바이스 — 완료

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

**언제 무엇을 쓰는가**

| sysfs | 캐릭터 디바이스 |
| --- | --- |
| 설정값 몇 개를 읽고 쓰기 | 데이터를 스트림으로 주고받기 |
| 값 하나당 파일 하나, 텍스트 | 바이너리 가능, 큰 데이터 |
| 상태 없음 | open~close 사이 상태 유지 |
| `cat`/`echo` 로 바로 확인 | `ioctl` 로 명령 전달 가능 |

연결이 유지되는 장치(시리얼·카메라·오디오)는 `open` 에서 하드웨어를 깨우고 `release` 에서 재우는 구조가 필요하므로 캐릭터 디바이스여야 합니다.

---

## 배운 것

**드라이버에 하드웨어 정보를 박지 않는다.**
핀 번호는 DTS에만 존재하고 드라이버는 받아쓰기만 합니다. 핀을 바꿔도 C 코드는 그대로고 DTS 한 줄만 고치면 됩니다. 리눅스가 수천 종의 ARM 보드를 하나의 커널로 지원하는 방식이 이것이라는 걸 직접 구현하며 이해했습니다.

**sysfs 파일은 파일이 아니다.**
디스크에 존재하지 않고, `cat`/`echo` 하는 순간 커널의 `show`/`store` 함수가 호출됩니다. 파일처럼 보이지만 실제로는 함수 호출이라 셸 스크립트든 파이썬이든 이 파일만 다루면 드라이버를 제어할 수 있습니다.

**`devm_` 접두사의 의미.**
`devm_gpiod_get()` 처럼 `devm_` 이 붙으면 디바이스 해제 시 커널이 자동으로 리소스를 반납합니다. `remove()` 에 해제 코드를 쓸 필요가 없어 누수 위험이 줄어듭니다.

**모듈 방식이 개발 속도를 좌우한다.**
`insmod` / `rmmod` 덕분에 재부팅 없이 반복 개발이 가능했습니다. 커널에 내장(`obj-y`)했다면 매번 전체 빌드와 재부팅이 필요했을 겁니다. 실제로 초기에 `drivers/misc/` 에 내장했다가 이름 충돌을 겪고 외부 모듈로 분리했습니다.

---

## 저장소 구성

```
.
├── drivers/chan_drv/        # 플랫폼 드라이버 (GPIO + sysfs)
├── overlays/                # 디바이스 트리 오버레이
├── scripts/build.sh         # 크로스 컴파일 스크립트
├── docs/                    # 검증 사진 및 로그
├── TROUBLESHOOTING.md       # 실패와 해결 기록
└── CHANGELOG.md
```

커널 소스 트리와 빌드 산출물은 저장소에 포함하지 않습니다. 직접 작성한 코드만 관리합니다.

---

## 빌드 방법

커널 소스는 [raspberrypi/linux](https://github.com/raspberrypi/linux) `rpi-6.12.y` 브랜치를 사용합니다.

```bash
# 외부 모듈 빌드
cd drivers/chan_drv && make

# 오버레이 컴파일
dtc -@ -I dts -O dtb -o my-device.dtbo overlays/my-device-overlay.dts
```

타깃 배포는 `scp` 후 `insmod` 하거나, 안정화되면 정식 위치에 설치합니다.

```bash
sudo cp chan_drv.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a && sudo modprobe chan_drv
```

---

## 다음 계획

- [ ] `ioctl` 추가 — 값이 아닌 **명령** 전달 (`LED_ON`, `LED_BLINK`)
- [ ] 전역 변수 제거 — 디바이스별 구조체 + `platform_set_drvdata()` 로 다중 인스턴스 대응
- [ ] 동시 접근 보호 — mutex 도입
- [ ] LED 서브시스템 — `led_classdev_register()` 로 `/sys/class/leds/` 표준 인터페이스 제공
- [ ] 입력 핀 + 인터럽트 — 버튼 입력에 대한 ISR 처리
- [ ] I2C 센서 연동 — 오버레이 `target = <&i2c1>` 형태 적용
