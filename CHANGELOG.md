# Changelog

## [Unreleased]
### Added
- 부팅 시퀀스 분석 및 부팅 시간 단축 (`docs/09-boot-analysis.md`)
  23.310s → 17.381s. 커널 구간은 2초로 이미 충분했고 유저스페이스가 원인이었다.
  `initcall_debug` 와 `systemd-analyze` 로 구간을 나눠 측정했다.

### In Progress
- `my_value` 와 GPIO 출력 사이의 경쟁 조건 수정 (락 4곳)
- top half / bottom half 분리
- `chan_drv` 를 `.dev_groups` 속성 그룹으로 전환
- BH1749NUC — `scale` 속성
- `ioctl` 인터페이스 — 값 전달이 아닌 명령 전달

## 2026-09-01
### Added
- `irq_count` / `bounce_count` sysfs 속성 — `dmesg` 없이 인터럽트 통계 확인
- GPIO 인터럽트 처리 (`drivers/chan_drv/`)
  - 오버레이에 pinctrl fragment 및 `button-gpios` 추가
  - `gpiod_to_irq()` + `devm_request_threaded_irq()` 로 ISR 등록
  - `jiffies` 기반 소프트웨어 디바운스 (`time_before()` 사용)
  - `spin_lock_irqsave()` 로 카운터 보호
  - `/proc/interrupts` 에 `chan_button` 등록 확인

센서 INT 핀이 보드에 인출되어 있지 않아 GPIO 스위치로 대체했다.
스위치의 풀 방식은 `gpioget` 으로 실측해 결정했다.

### Fixed
- `irq_count` 생성 실패 시 아직 만들지 않은 파일을 제거하는 경로로
  점프하던 문제 — `goto err_bounce` → `goto err_sysfs`

## 2026-08-28 (2)
### Added
- BH1749NUC IIO 서브시스템 등록 — `/sys/bus/iio/devices/iio:deviceN/`
  - `iio_chan_spec` 로 RED/GREEN/BLUE/IR 채널 선언
  - `read_raw` 콜백 하나가 `chan->address` 로 네 채널 처리
  - `devm_iio_device_alloc()` + `iio_priv()` 로 인스턴스별 데이터 분리
  - `devm_add_action_or_reset()` 로 해제 시 센서 전원 차단
  - `iio_info` 등 기존 도구가 별도 작업 없이 동작함을 확인

### Removed
- 직접 구현했던 sysfs 속성 일체 (`show` 함수, 속성 매크로, `.dev_groups`)
  — IIO가 표준 경로를 생성하므로 불필요

### Docs
- README를 표지로 재구성하고 단계별 상세를 `docs/01`~`docs/07` 로 분리

## 2026-08-28
### Added
- BH1749NUC 측정값 읽기 구현
  - 데이터시트 기반 초기화 시퀀스 (SW reset → 게인/측정주기 → RGB_EN)
  - `i2c_smbus_read_word_data()` 로 16비트 채널값 원자적 읽기
  - RED / GREEN / BLUE / IR / GREEN2 / VALID sysfs 속성
  - `.dev_groups` 로 속성 그룹 등록 — 생성·제거를 커널이 처리
  - `devm_kzalloc` + `i2c_set_clientdata` 로 인스턴스별 상태 분리
  - 두 센서가 서로 다른 값을 보고하는 것으로 분리 동작 확인

### Fixed
- `GENMASK()` 결과를 `%02x` 로 출력하며 발생한 타입 불일치 경고 — `(u8)` 캐스트

## 2026-08-27
### Added
- BH1749NUC I2C 드라이버 골격 (`drivers/bh1749/`)
  - 디바이스 트리 오버레이 — `target = <&i2c1>`, 노드 2개(`@38`, `@39`)
  - `i2c_driver` probe — `i2c_check_functionality()` 및 제조사 ID 검사
  - `MANUFACTURER_ID` = `0xE0`, `PART_ID` = `0x0D` 확인
  - 보드에 동일 칩 2개가 실장되어 probe가 두 번 호출됨을 확인

## 2026-08-26
### Added
- LED 서브시스템 등록 — `/sys/class/leds/chan:led/` 표준 인터페이스 제공
  - `devm_led_classdev_register()` 로 등록, `brightness_set`/`brightness_get` 콜백 구현
  - `trigger` (heartbeat, timer, cpu 등) 가 별도 구현 없이 동작함을 확인
  - LED 등록 실패 시 `device_create` 까지 되감도록 `err_device` 라벨 추가
- README에 검증 사진 삽입 자리(`<사진>`) 10곳 표시, `docs/README.md` 로 필요 목록 정리

### Changed
- `class_create()` 실패 시 반환 코드를 `-ENODEV` 에서 `PTR_ERR()` 로 변경

### Fixed
- TROUBLESHOOTING 3번 보강 — `tee` 가 파일 부재를 `Permission denied` 로 보고하는
  이유(sysfs 파일 생성 불가 → `EACCES`)와 `ls`/`cat` 으로 구분하는 방법

## 2026-08-25
### Added
- 캐릭터 디바이스 인터페이스 구현 완료 — `/dev/my_device` 로 `read()`/`write()` 동작 검증
  - `alloc_chrdev_region()` / `cdev_add()` / `class_create()` + `device_create()` 3단계 등록
  - `copy_from_user()` / `simple_read_from_buffer()` 로 유저 공간 메모리 안전 접근
  - 등록의 역순으로 되감는 계단식 `goto` 에러 처리
- 드라이버 소스 및 빌드 Makefile 저장소에 추가 (`drivers/chan_drv/`)

### Fixed
- `class_create()` 실패 시 생성되지 않은 클래스를 `class_destroy()` 하던 문제
  - 컴파일 경고(`label 'err_cdev' defined but not used`)가 실제 버그를 가리키고 있었음
  - 반환 코드도 `-ENODEV` 대신 `PTR_ERR()` 로 실제 원인을 전달하도록 수정

## 2026-08-23
### Changed
- 보드 DTS 직접 수정에서 디바이스 트리 오버레이 방식으로 전환
  - 반복 주기에서 커널 빌드 제거 (`make dtbs` → `dtc`)
  - 커널 업데이트 시 수정사항 소실 문제 해결

### Added
- GPIO 플랫폼 드라이버 구현 완료 — DTS 선언부터 실제 LED 점등까지 검증
  - `devm_gpiod_get()` 으로 GPIO 17 확보
  - sysfs `value` 속성으로 유저 공간 제어

### Fixed
- 모듈 빌드 시 커널 전체가 재빌드되던 문제 — `M=$(PWD)` → `M=$(CURDIR)`
  (`sudo` 가 환경변수 `PWD` 를 제거해 `M=` 이 비어 있었음)

## 2026-08-15
### Fixed
- 커널 이미지 교체 후 부팅 불가 — 32비트 빌드 설정 수정 및 모듈 이식
  - `arm` → `arm64`, `bcm2709_defconfig` → `bcm2711_defconfig`, `zImage` → `Image`
  - `/lib/modules/6.12.28-v8+` 타깃 이식
