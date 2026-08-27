# LED 서브시스템 — 완료

3절에서 sysfs 파일(`value`)을 직접 만들었습니다. 같은 기능을 이번엔 커널의 **LED 서브시스템에 등록**하는 방식으로 구현해 두 접근을 비교했습니다.

| | 직접 만든 sysfs | LED 서브시스템 |
| --- | --- | --- |
| 경로 | `/sys/devices/platform/my_device/value` | `/sys/class/leds/chan:led/brightness` |
| 파일을 정의한 주체 | 내가 `DEVICE_ATTR_RW()` 로 | 커널이 등록을 받고 |
| 이름·규칙 | 내 마음대로 | 커널이 정함 |
| 파싱·범위 검사 | 내가 구현 | 커널이 처리 |
| 깜빡임 패턴 | 타이머 직접 구현 | `trigger` 로 제공됨 |

**드라이버가 구현할 것은 콜백 하나로 줄어듭니다.**

```c
static void chan_led_set(struct led_classdev *cdev, enum led_brightness b)
{
	if (my_gpio)
		gpiod_set_value(my_gpio, b ? 1 : 0);

	my_value = b;
}
```

```c
chan_led.name = "chan:led";
chan_led.max_brightness = LED_ON;
chan_led.brightness_set = chan_led_set;
chan_led.brightness_get = chan_led_get;

ret = devm_led_classdev_register(dev, &chan_led);
```

`max_brightness = LED_ON`(=1)은 켜짐/꺼짐만 있다는 뜻입니다. PWM으로 밝기 조절이 되는 LED면 255를 줍니다. 이름의 콜론(`chan:led`)은 `장치:색상:기능` 형식의 관례입니다.

**검증**

```
my_dev_drv my_device: led ready: /sys/class/leds/chan:led/brightness
```

```bash
echo 1 | sudo tee /sys/class/leds/chan:led/brightness
```

**표준을 따르면 따라오는 것 — `trigger`**

```bash
cat /sys/class/leds/chan:led/trigger        # 사용 가능한 트리거 목록
echo heartbeat | sudo tee /sys/class/leds/chan:led/trigger
```

LED가 심장박동 패턴으로 점멸합니다. **해당 코드는 작성하지 않았습니다.** 커널의 heartbeat 트리거가 타이머를 돌리며 `chan_led_set()` 을 반복 호출합니다. `timer` 트리거를 쓰면 `delay_on` / `delay_off` 로 주기도 지정할 수 있습니다.

터미널에서 트리거를 조작한 화면입니다.

<img width="857" alt="LED trigger 조작" src="https://github.com/user-attachments/assets/f8a43ac2-c642-451b-9ef5-a38dadded737" />

```bash
ls /sys/class/leds/chan:led/
cat /sys/class/leds/chan:led/trigger
echo heartbeat | sudo tee /sys/class/leds/chan:led/trigger
dmesg | tail -3
```

점멸 자체는 정지 화면에 담기지 않으므로, **`trigger` 목록에서 `[heartbeat]` 가 선택 표시된 출력**이 그 역할을 대신합니다. 커널이 패턴을 맡고 있다는 증거입니다.

VS Code(WSL 원격)에서 `chan_drv.c` 를 수정하고 통합 터미널에서 크로스 컴파일한 화면입니다. x86 WSL에서 aarch64 모듈을 빌드하고 있습니다.

<img width="1310" alt="VS Code WSL 원격 빌드" src="https://github.com/user-attachments/assets/049b4e37-dbf0-493c-a990-c918a05f86fc" />

> **참고** — 현재 드라이버는 같은 GPIO를 세 경로(`value`, `/dev/my_device`, `brightness`)로 제어합니다. **비교 학습을 위해 의도적으로 병기한 것이며, 실제 제품이라면 LED 서브시스템 하나만 남깁니다.**

---

[← 5. 캐릭터 디바이스](05-char-device.md) · [목차](../README.md) · [7. I2C 컬러센서 (BH1749NUC) →](07-i2c-bh1749.md)
