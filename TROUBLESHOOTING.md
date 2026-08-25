# Troubleshooting

실제로 막혔던 문제와 해결 과정입니다. 증상만으로는 원인이 드러나지 않았던 것들을 중심으로 정리했습니다.

---

## 1. 커널 이미지 교체 후 부팅 불가

**증상** — 크로스 컴파일한 `kernel8.img` / `bcm2711-rpi-4-b.dtb` 를 SD카드 `bootfs` 에 복사 후 부팅하면 초록색 ACT LED가 반복 점멸한 뒤 무반응. 화면 출력 없음.

**원인이 두 개 연달아 있었습니다.** 둘 다 고쳐야 부팅됩니다. 하나만 고치면 증상이 똑같이 보여서 오래 헤맸습니다.

### 원인 1 — 32비트 빌드 설정

타깃은 RPi4 64비트인데 빌드 스크립트가 RPi2/3용 32비트 기준이었습니다. `bcm2709`는 RPi2/3용, RPi4는 `bcm2711`입니다. 32비트 `zImage`를 `kernel8.img`로 이름만 바꿔 넣은 셈이라 부트로더가 로드하자마자 죽는 게 당연했습니다.

| 항목 | 문제 | 수정 |
| --- | --- | --- |
| `KERNEL` | `kernel7` | `kernel8` |
| `ARCH` | `arm` | `arm64` |
| `CROSS_COMPILE` | `arm-linux-gnueabihf-` | `aarch64-linux-gnu-` |
| defconfig | `bcm2709_defconfig` | `bcm2711_defconfig` |
| 빌드 타깃 | `zImage` | `Image` |

### 원인 2 — 모듈 누락으로 Kernel Panic

이미지를 고친 뒤에도 부팅 중 파닉이 났습니다. 커널(`6.12.28-v8+`)은 올라왔지만 매칭되는 모듈이 타깃에 없었습니다. SD카드의 `/lib/modules/` 에는 순정 커널(`6.18.38-v8`) 모듈만 있었습니다.

```bash
make O=out ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     modules_install INSTALL_MOD_PATH=./modules_out
```

생성된 모듈을 타깃의 `/lib/modules/` 로 옮기니 정상 부팅했습니다.

### 다음에 커널 교체할 때 체크리스트

- [ ] 빌드 설정이 `arm64` / `bcm2711_defconfig` / `Image` 인지
- [ ] 타깃의 `uname -r` 과 `/lib/modules/` 하위 폴더명이 일치하는지
- [ ] 커널 이미지 · DTB · 모듈 **3종 세트**를 같이 옮겼는지
- [ ] 순정 이미지 백업을 미리 확보했는지

> **교훈** — 커널 교체는 이미지만 옮기면 되는 게 아니다. `kernel8.img` + `*.dtb` + `/lib/modules/<버전>` 3종 세트다.

---

## 2. 모듈 하나 빌드하려는데 커널 전체가 재빌드됨

**증상** — 외부 모듈(`chan_drv.ko`) 하나만 빌드하려 했는데 커널 전체 모듈이 빌드되며 1시간 이상 소요. `sound/soc/` 등 전혀 무관한 파일이 컴파일됨.

**원인** — Makefile이 `M=$(PWD)` 를 사용했습니다. `$(PWD)`는 셸이 환경변수로 전달하는 값인데, **`sudo`가 보안상 환경변수를 제거하면서 `PWD`가 사라집니다.** 결과적으로 `M=` 이 빈 값이 되어 make가 "전체 트리"로 해석했습니다.

```
make -C .../linux O=.../out ARCH=arm64 ... M= modules
                                          ↑ 비어 있음
```

`sudo` 없이 실행하면 정상 동작해서 증상이 간헐적으로 보인 것도 혼란의 원인이었습니다.

**해결** — `$(PWD)` → `$(CURDIR)`. `$(CURDIR)`는 make가 자체 계산하는 내장 변수라 `sudo` 영향을 받지 않습니다.

> **교훈** — 커널 빌드 Makefile에서는 `$(PWD)` 대신 `$(CURDIR)`. 빌드가 예상보다 오래 걸리면 `ps` 로 실제 명령줄의 `M=` 값부터 확인.

---

## 3. `Permission denied` 인데 권한 문제가 아니었던 경우

**증상** — 재부팅 후 `echo 1 > /sys/devices/platform/my_device/value` 가 `Permission denied` 로 실패.

**원인** — `/tmp` 에 두었던 `chan_drv.ko` 가 재부팅으로 사라져 `insmod` 가 실패했고, 그래서 sysfs 파일 자체가 생성되지 않았습니다. 즉 **권한 문제가 아니라 파일 부재**였습니다. 메시지만 보면 오해하기 쉽습니다.

**해결** — 모듈을 정식 위치에 설치.

```bash
sudo mkdir -p /lib/modules/$(uname -r)/extra
sudo cp chan_drv.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a && sudo modprobe chan_drv
```

---

## 4. 오버레이가 조용히 적용 실패

**증상** — `.dtbo` 를 복사하고 `config.txt` 에 등록했는데 노드가 나타나지 않음. 에러 메시지도 없음.

**원인** — 오버레이에서 `&gpio` 같은 **라벨**을 쓰려면 베이스 DTB에 심볼 테이블이 있어야 합니다. 없으면 라벨을 못 찾아 조용히 실패합니다.

```bash
ls /proc/device-tree/__symbols__/ | grep -x gpio
```

**해결** — `dtc` 컴파일 시 `-@`, 커널 DTB 빌드 시 `DTC_FLAGS=-@` 를 추가합니다.

부수적으로 겪은 것: `__overlay__ = {` 처럼 `=` 를 넣어 syntax error. DTS에서 `=` 는 프로퍼티에 값을 대입할 때만 쓰고, 노드는 이름 뒤에 바로 중괄호입니다.

---

## 5. 드라이버 이름 충돌

초기에 `drivers/misc/my_driver.c` 를 `obj-y` 로 커널에 내장한 채로 동일 기능의 외부 모듈을 만들어 충돌이 발생했습니다. 외부 모듈로 일원화하면서 해결했고, 이 과정에서 내장 방식과 모듈 방식의 개발 반복 주기 차이를 체감했습니다.

또한 DTS의 `compatible` 문자열에 공백이 들어간 채(`"chan, my-device"`) 작성되어 매칭 확인에 시간을 쓴 적이 있습니다. 문자열은 정확히 일치해야 합니다.
