# docs

README 본문의 **<사진>** 표시 자리에 넣을 검증 자료를 모아두는 곳입니다.

촬영/캡처 후 이 디렉터리에 넣고, README의 해당 `<사진>` 줄을 이미지 문법으로 교체합니다.

```markdown
![LED 점등](docs/led-on.jpg)
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

사진은 **내용이 읽히는 해상도**로. 터미널 캡처는 폰트가 작으면 의미가 없습니다.
