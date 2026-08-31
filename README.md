# RPi4 Linux Device Driver Practice

라즈베리파이 4B(64bit)에서 커널을 직접 크로스 컴파일하고, 디바이스 트리부터 I2C 센서 드라이버까지 리눅스 디바이스 드라이버를 단계적으로 구현한 **개인 학습 프로젝트**입니다.

> 실무 프로젝트가 아닌 학습 목적의 개인 프로젝트입니다. 모든 코드는 직접 작성했고, 모든 동작은 실제 RPi4 보드에서 검증했습니다.

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

각 단계는 **실제 하드웨어에서 동작을 확인한 것만** 완료로 표기했습니다.

| 단계 | 내용 | 핵심 |
| --- | --- | --- |
| [1](docs/01-cross-compile.md) | 커널 크로스 컴파일 및 교체 | `bcm2711_defconfig`, 커널·DTB·모듈 3종 세트 |
| [2](docs/02-platform-driver.md) | 플랫폼 드라이버 + DTS 매칭 | `of_device_id`, `compatible`, `probe()` |
| [3](docs/03-sysfs-gpio.md) | sysfs + GPIO 제어 | `devm_gpiod_get()`, `show`/`store` |
| [4](docs/04-device-tree-overlay.md) | 디바이스 트리 오버레이 전환 | `.dtbo`, `dtc -@`, `config.txt` |
| [5](docs/05-char-device.md) | 캐릭터 디바이스 | `cdev`, `file_operations`, `copy_from_user()` |
| [6](docs/06-led-subsystem.md) | LED 서브시스템 | `led_classdev`, `trigger` |
| [7](docs/07-i2c-bh1749.md) | I2C 컬러센서 (BH1749NUC) | `i2c_driver`, IIO 서브시스템, 데이터시트 기반 구현 |

7단계는 **메인라인 커널에 드라이버가 없는 칩**을 대상으로 했습니다. 데이터시트를 읽고 레지스터 맵과 초기화 시퀀스를 직접 구현한 뒤, IIO 서브시스템에 등록해 `in_intensity_red_raw` 같은 표준 경로를 제공합니다. 보드에 동일 칩이 두 개 실장되어 있어 인스턴스별 상태 분리도 함께 다뤘습니다.

---

## 배운 것

**드라이버에 하드웨어 정보를 박지 않는다.**
핀 번호는 DTS에만 존재하고 드라이버는 받아쓰기만 합니다. 핀을 바꿔도 C 코드는 그대로고 DTS 한 줄만 고치면 됩니다. 리눅스가 수천 종의 ARM 보드를 하나의 커널로 지원하는 방식이 이것이라는 걸 직접 구현하며 이해했습니다.

**sysfs 파일은 파일이 아니다.**
디스크에 존재하지 않고, `cat`/`echo` 하는 순간 커널의 `show`/`store` 함수가 호출됩니다. 파일처럼 보이지만 실제로는 함수 호출이라 셸 스크립트든 파이썬이든 이 파일만 다루면 드라이버를 제어할 수 있습니다.

**`devm_` 접두사의 의미.**
`devm_gpiod_get()` 처럼 `devm_` 이 붙으면 디바이스 해제 시 커널이 자동으로 리소스를 반납합니다. `remove()` 에 해제 코드를 쓸 필요가 없어 누수 위험이 줄어듭니다.

**서브시스템은 인터페이스의 "종류"가 아니라 "표준화"다.**
LED 서브시스템의 결과물도 결국 sysfs 파일입니다. 차이는 그 파일을 **누가 정의했느냐**입니다. 드라이버 작성자가 각자 이름을 정하면 유저 공간이 제조사별 분기를 짜야 하지만, 표준을 따르면 `/sys/class/leds/*/brightness` 하나로 모든 LED를 다룰 수 있습니다. IIO·input·hwmon도 같은 구조입니다.

**모듈 방식이 개발 속도를 좌우한다.**
`insmod` / `rmmod` 덕분에 재부팅 없이 반복 개발이 가능했습니다. 커널에 내장(`obj-y`)했다면 매번 전체 빌드와 재부팅이 필요했을 겁니다. 실제로 초기에 `drivers/misc/` 에 내장했다가 이름 충돌을 겪고 외부 모듈로 분리했습니다.

---

## 실패와 해결

막혔던 문제와 원인 추적 과정을 [TROUBLESHOOTING.md](TROUBLESHOOTING.md) 에 정리했습니다. 몇 가지만 옮기면,

- **커널 이미지 교체 후 부팅 불가** — 원인이 32비트 빌드 설정과 모듈 누락 두 개가 겹쳐 있었습니다. 하나만 고치면 증상이 동일해 오래 헤맸습니다.
- **모듈 하나 빌드하려는데 커널 전체가 재빌드됨** — `sudo` 가 환경변수 `PWD` 를 제거해 `M=` 이 빈 값이 되고 있었습니다. `ps` 로 실제 명령줄을 확인해서 특정했습니다.
- **컴파일 경고가 실제 버그를 가리키고 있던 경우** — `label defined but not used` 경고가, 생성 실패한 클래스를 `class_destroy()` 하게 되는 경로를 드러내고 있었습니다.

---

## 저장소 구성

```
.
├── drivers/chan_drv/        # 플랫폼 드라이버 (GPIO + sysfs + chardev + LED)
├── drivers/bh1749/          # I2C 컬러센서 드라이버 (BH1749NUC)
├── overlays/                # 디바이스 트리 오버레이
├── scripts/build.sh         # 크로스 컴파일 스크립트
├── docs/                    # 단계별 상세 기록
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

- [ ] BH1749NUC — `scale` 속성 추가 및 게인 변경 지원
- [ ] 인터럽트 처리 — 센서 INT 핀에 대한 ISR 과 bottom half
- [ ] 동시 접근 보호 — mutex 도입 및 경쟁 상태 재현
- [ ] `chan_drv` 전역 변수 제거 — 디바이스별 구조체 + `platform_set_drvdata()`
- [ ] `ioctl` 추가 — 값이 아닌 **명령** 전달
