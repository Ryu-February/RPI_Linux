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
- 캐릭터 디바이스 인터페이스 *(진행 중)*

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

### 5. 캐릭터 디바이스 — 진행 중

`/dev/my_device` 노드를 만들어 `read()` / `write()` / `ioctl()` 로 제어하는 인터페이스를 구현 중입니다.

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

- [ ] 캐릭터 디바이스 — `/dev/my_device` 로 `read()`/`write()`/`ioctl()`
- [ ] LED 서브시스템 — `led_classdev_register()` 로 `/sys/class/leds/` 표준 인터페이스 제공
- [ ] 입력 핀 + 인터럽트 — 버튼 입력에 대한 ISR 처리
- [ ] I2C 센서 연동 — 오버레이 `target = <&i2c1>` 형태 적용
