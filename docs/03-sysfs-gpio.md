# sysfs + GPIO 제어 — 완료

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

배선: GPIO 17 (물리 11번) → LED → GND 

RPi4 + 브레드보드 배선 전경. GPIO 17 → 저항 → LED → GND.

<img width="807" alt="RPi4 브레드보드 배선" src="https://github.com/user-attachments/assets/e4ed0219-a3cf-4cbf-a016-01d3363b62ca" />

`echo 1` 로 LED가 점등된 상태

<img width="571" alt="echo 1 로 LED가 점등된 상태" src="https://github.com/user-attachments/assets/48fd9e84-99af-4fdd-91d2-387021a89bae" />

---

[← 2. 플랫폼 드라이버 + DTS 매칭](02-platform-driver.md) · [목차](../README.md) · [4. 디바이스 트리 오버레이 전환 →](04-device-tree-overlay.md)
