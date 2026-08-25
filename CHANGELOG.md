# Changelog

## [Unreleased]
### In Progress
- `ioctl` 인터페이스 — 값 전달이 아닌 명령 전달
- I2C 센서(BH1749NUC) 드라이버 — IIO 서브시스템 등록

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
