# docs

README 본문의 **<사진>** 표시 자리에 넣을 검증 자료를 모아두는 곳입니다.

촬영/캡처 후 이 디렉터리에 넣고, README의 해당 `<사진>` 줄을 이미지 문법으로 교체합니다.

```markdown
![LED 점등](led-on.jpg)
```

## 필요한 목록

| 파일명(예시) | 내용 |
| --- | --- |
| `cross-build.png` | WSL 크로스 컴파일 진행/완료 화면 |
| `uname.png` | 타깃 `uname -a` — 직접 빌드한 커널로 부팅 확인 |
| `wiring.jpg` | RPi4 + 브레드보드 배선 전경 |
| `led-on.jpg` | `echo 1` 로 LED 점등된 상태 |
| `gpio-code.png` | `chan_drv.c` 의 `devm_gpiod_get()` 부분 |
| `config-txt.png` | `config.txt` 의 `dtoverlay=my-device` 등록 |
| `device-tree.png` | `ls /proc/device-tree/my_device/` 출력 |
| `dmesg-chardev.png` | `insmod` 직후 `dmesg` — probe 및 chardev 로그 |
| `dev-node.png` | `ls -l /dev/my_device` + `cat /dev/my_device` |
| `rmmod-compare.png` | `rmmod` 전후 sysfs 파일 유무 비교 |
| `led-trigger-shell.png` | 터미널에서 `trigger` 조작 — `[heartbeat]` 선택 표시가 보이도록 |
| `vscode-build.png` | VS Code(WSL 원격) — 코드와 빌드 로그를 한 화면에 |
| `i2c-detect.png` | `i2cdetect` 주소 표 + `i2cget` 의 `0xe0` 출력 |
| `i2c-probe.png` | `dmesg` probe 로그(두 인스턴스) + `i2cdetect` 의 `UU UU` |
| `i2c-sensor-read.png` | 두 인스턴스의 RGB/IR 값과 한쪽을 가렸을 때의 변화 |

사진은 **내용이 읽히는 해상도**로. 터미널 캡처는 폰트가 작으면 의미가 없습니다.

## 캡처 요령

- **터미널** — 명령과 출력이 함께 보이게. 스크롤이 잘려 명령만 보이거나 출력만 보이면 근거가 약해집니다.
- **글자 크기** — 축소된 캡처는 GitHub에서 읽히지 않습니다. 폰트를 키우고 창을 좁혀서 찍는 편이 낫습니다.
- **한 장에 하나의 주장** — "트리거가 커널 몫이다", "크로스 빌드가 돈다" 처럼 그 사진이 증명하는 것을 하나로.
- **파일 형식** — 터미널·코드는 `.png`, 실물 사진은 `.jpg`.
