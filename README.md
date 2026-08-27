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
- **LED 서브시스템** 등록 — 직접 만든 sysfs를 커널 표준 인터페이스로 대체
- I2C 통신 확인 및 **메인라인 미지원 칩(BH1749NUC)** 드라이버 작성 *(진행 중)*

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

**<<img width="982" height="507" alt="image" src="https://github.com/user-attachments/assets/43e430b6-474c-458a-a50c-370609a3ac4c" />
>** — WSL(우분투) 크로스 컴파일 진행 화면

**<<img width="618" height="66" alt="image" src="https://github.com/user-attachments/assets/3c232923-1663-41cb-8166-db0fd77d0966" />
>** — 타깃에서 `uname -a` 출력. 직접 빌드한 `6.12.28-v8+` 커널로 부팅된 것을 보여주는 화면

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

배선: GPIO 17 (물리 11번) → LED → GND 

**<<img width="807" height="463" alt="image" src="https://github.com/user-attachments/assets/e4ed0219-a3cf-4cbf-a016-01d3363b62ca" />
 />
>** — RPi4 + 브레드보드 배선 전경.

**<<img width="571" height="572" alt="image" src="https://github.com/user-attachments/assets/48fd9e84-99af-4fdd-91d2-387021a89bae" />
>** — `echo 1` 로 LED가 점등된 상태

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

**<<img width="954" height="968" alt="image" src="https://github.com/user-attachments/assets/cbc75684-e513-48ef-8690-7cf7b5da972a" />
>** — 타깃의 `/boot/firmware/config.txt` 에 `dtoverlay=my-device` 가 등록된 화면(xshell 캡처화면)

**<<img width="414" height="110" alt="image" src="https://github.com/user-attachments/assets/b830cd08-ef41-473e-98e3-e2c92834f7b6" />
>** — `ls /proc/device-tree/my_device/` 출력. 

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

**<<img width="852" height="155" alt="image" src="https://github.com/user-attachments/assets/26176b14-2dc8-401b-94e2-3d5e96a5e950" />
>** — `insmod` 직후 `dmesg` 출력. probe 로그와 `chardev ready` 줄이 보이도록

**<<img width="509" height="63" alt="image" src="https://github.com/user-attachments/assets/0d0297e1-9b7c-4e7a-9b88-90ddc2d0bd6e" />
>** — `ls -l /dev/my_device` 와 `cat /dev/my_device` 를 연달아 실행한 화면

**<<img width="491" height="80" alt="image" src="https://github.com/user-attachments/assets/bfe1cadb-1eeb-4a79-8b0f-8b9e04dd3654" />
>** — `rmmod` 전후로 `ls /sys/devices/platform/my_device/` 를 비교한 화면 (`permission denied` 사진)

**언제 무엇을 쓰는가**

| sysfs | 캐릭터 디바이스 |
| --- | --- |
| 설정값 몇 개를 읽고 쓰기 | 데이터를 스트림으로 주고받기 |
| 값 하나당 파일 하나, 텍스트 | 바이너리 가능, 큰 데이터 |
| 상태 없음 | open~close 사이 상태 유지 |
| `cat`/`echo` 로 바로 확인 | `ioctl` 로 명령 전달 가능 |

연결이 유지되는 장치(시리얼·카메라·오디오)는 `open` 에서 하드웨어를 깨우고 `release` 에서 재우는 구조가 필요하므로 캐릭터 디바이스여야 합니다.

---

### 6. LED 서브시스템 — 완료

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

**<<img width="857" height="291" alt="image" src="https://github.com/user-attachments/assets/f8a43ac2-c642-451b-9ef5-a38dadded737" />
>** — `docs/led-trigger-shell.png`
터미널(Xshell)에서 트리거를 실제로 조작한 화면. 한 화면에 아래가 함께 보이면 좋습니다.

```bash
ls /sys/class/leds/chan:led/
cat /sys/class/leds/chan:led/trigger
echo heartbeat | sudo tee /sys/class/leds/chan:led/trigger
dmesg | tail -3
```

점멸 자체는 정지 화면에 담기지 않으므로, **`trigger` 목록에서 `[heartbeat]` 가 선택 표시된 출력**이 그 역할을 대신합니다. 커널이 패턴을 맡고 있다는 증거입니다.

**<<img width="1310" height="978" alt="image" src="https://github.com/user-attachments/assets/049b4e37-dbf0-493c-a990-c918a05f86fc" />
>** — `docs/vscode-build.png`
VS Code(WSL 원격)에서 `chan_drv.c` 의 `chan_led_set()` 부분과 통합 터미널의 빌드 로그(`CC [M] chan_drv.o`)가 한 화면에 잡힌 캡처. 코드와 빌드 결과를 같이 담아 **크로스 컴파일 환경**을 보여줍니다.

> **참고** — 현재 드라이버는 같은 GPIO를 세 경로(`value`, `/dev/my_device`, `brightness`)로 제어합니다. **비교 학습을 위해 의도적으로 병기한 것이며, 실제 제품이라면 LED 서브시스템 하나만 남깁니다.**

### 7. I2C 컬러센서 (BH1749NUC) — 진행 중

**메인라인에 드라이버가 없는 칩**을 대상으로 골랐습니다. 커널 트리에는 같은 벤더의 인접 모델만 있습니다.

| 칩 | 커널 드라이버 |
| --- | --- |
| BH1745 (RGB+C) | `drivers/iio/light/bh1745.c` |
| BH1750 (조도) | `drivers/iio/light/bh1750.c` |
| BH1780 (조도) | `drivers/iio/light/bh1780.c` |
| **BH1749NUC (RGB+IR)** | **없음** |

기존 드라이버를 적재해 동작을 확인하는 것과, **데이터시트를 읽고 지원되지 않는 칩을 동작시키는 것**은 다른 작업입니다. 후자를 해보기 위해 이 칩을 택했습니다. `bh1745.c` 는 `regmap` 과 IIO triggered buffer를 사용하는 최신 구조라 참고 대상으로도 적절합니다.

#### 하드웨어 준비

BH1749NUC가 실장된 기존 보드에서 I2C 4선(VCC · GND · SDA · SCL)을 직접 인출해 RPi4의 GPIO 헤더에 연결했습니다.

| 신호 | RPi4 물리 핀 |
| --- | --- |
| VCC | 1번 (3.3V) |
| GND | 6번 |
| SDA | 3번 (GPIO2) |
| SCL | 5번 (GPIO3) |

브레이크아웃 모듈을 구입하는 대신 실장된 칩에서 배선한 것은, **데이터시트만 가지고 통신을 성립시키는 과정 자체를 겪어보기 위해서**였습니다. 3.3V 소자이고 RPi의 I2C 라인도 3.3V라 레벨 변환 없이 직결했습니다.

#### 통신 확인

```bash
sudo raspi-config nonint do_i2c 0     # 또는 config.txt 에 dtparam=i2c_arm=on
sudo i2cdetect -y 1
```

주소가 잡히면 **기대값을 아는 고정 레지스터**를 먼저 읽습니다.

```bash
sudo i2cget -y 1 0x38 0x92
# 0xe0
```

| 레지스터 | 주소 | 읽은 값 | 의미 |
| --- | --- | --- | --- |
| `MANUFACTURER_ID` | `0x92` | `0xE0` | ROHM 제조사 ID |

데이터시트: [ROHM BH1749NUC](https://www.rohm.com/products/sensors-mems/color-sensor-ics/bh1749nuc-product)

**센서 값보다 ID 레지스터를 먼저 읽는 이유**는 통신 경로 전체를 한 번에 검증하기 위해서입니다. 이 값이 나왔다는 것은 배선 극성, 전원, 주소, 칩 응답이 모두 정상이라는 뜻입니다. 센서 값부터 읽으면 결과가 이상할 때 배선 문제인지 설정 문제인지 구분할 수 없습니다.

드라이버 `probe()` 에서도 같은 검사를 수행하고, 값이 맞지 않으면 probe를 실패시키는 것이 관례입니다.

**<사진>** — `docs/i2c-detect.png` : `i2cdetect` 주소 표와 `i2cget` 의 `0xe0` 출력이 한 화면에

#### 디바이스 트리 오버레이

`target-path` 대신 **`target = <&i2c1>`** 을 씁니다. I2C 장치는 컨트롤러 노드 안에 있어야 커널이 해당 버스의 장치로 등록합니다. 루트 밑에 두면 플랫폼 디바이스가 되어 I2C로 인식되지 않습니다.

```dts
fragment@0 {
	target = <&i2c1>;
	__overlay__ {
		#address-cells = <1>;
		#size-cells = <0>;
		status = "okay";

		bh1749_a: light-sensor@38 {
			compatible = "rohm,bh1749";
			reg = <0x38>;
			status = "okay";
		};

		bh1749_b: light-sensor@39 {
			compatible = "rohm,bh1749";
			reg = <0x39>;
			status = "okay";
		};
	};
};
```

`reg` 가 I2C 주소이고 노드 이름의 `@38` 과 일치해야 합니다. `#address-cells = <1>` / `#size-cells = <0>` 은 자식 노드의 `reg` 해석 방식을 지정합니다 — I2C는 주소 1개, 크기 없음.

**보드에 같은 칩이 두 개 실장되어 있어** 노드를 두 개 선언했습니다. BH1749NUC는 ADDR 핀으로 주소가 갈리므로 `0x38` 과 `0x39` 를 동시에 사용합니다.

```bash
ls /sys/bus/i2c/devices/
# 1-0038  1-0039  i2c-1  i2c-20  i2c-21
```

#### i2c_driver

플랫폼 드라이버와 골격은 같지만 버스가 다릅니다.

| | `platform_driver` | `i2c_driver` |
| --- | --- | --- |
| 등록 매크로 | `module_platform_driver()` | `module_i2c_driver()` |
| probe 인자 | `struct platform_device *` | `struct i2c_client *` |
| 주소 정보 | 없음 | `client->addr` 에 이미 채워짐 |
| 통신 | 직접 | `i2c_smbus_*` 헬퍼 |

```c
static int bh1749_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int ret;

	dev_info(dev, "probe called (addr=0x%02x)\n", client->addr);

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;

	ret = i2c_smbus_read_byte_data(client, BH1749_MANUFACTURER_ID);
	if (ret < 0)
		return ret;

	dev_info(dev, "manufacturer id = 0x%02x\n", ret);

	if (ret != BH1749_MANUFACTURER_ROHM) {
		dev_err(dev, "unexpected manufacturer id 0x%02x\n", ret);
		return -ENODEV;
	}
	...
}
```

**드라이버 코드 어디에도 `0x38` 이 없습니다.** `client->addr` 은 DTS의 `reg` 를 커널이 읽어 채워준 값입니다. 주소를 바꾸려면 오버레이 한 줄만 고치면 됩니다 — GPIO 절에서 핀 번호를 다룬 방식과 동일합니다.

`i2c_check_functionality()` 로 어댑터가 필요한 전송 방식을 지원하는지 먼저 확인하고, 칩 ID가 기대값과 다르면 `-ENODEV` 로 probe를 실패시킵니다. 같은 주소에 다른 칩이 있을 수 있기 때문입니다.

**검증 결과**

```
bh1749 1-0039: probe called (addr=0x39)
bh1749 1-0039: manufacturer id = 0xe0
bh1749 1-0039: part id = 0x0d (raw 0x0d)
bh1749 1-0039: chip detected
bh1749 1-0038: probe called (addr=0x38)
bh1749 1-0038: manufacturer id = 0xe0
bh1749 1-0038: part id = 0x0d (raw 0x0d)
bh1749 1-0038: chip detected
```

```bash
$ ls -l /sys/bus/i2c/devices/1-0038/driver
... -> ../../../../../../bus/i2c/drivers/bh1749

$ sudo i2cdetect -y 1
30: -- -- -- -- -- -- -- -- UU UU -- -- -- -- -- --
```

**주소가 `UU` 로 바뀐 것이 소유권 이전의 증거입니다.** `UU` 는 Used/Unavailable, 즉 드라이버가 점유 중이라 `i2cdetect` 가 프로브하지 않았다는 뜻입니다. 이제 이 칩은 유저 공간 도구가 아니라 드라이버가 관리합니다.

**<사진>** — `docs/i2c-probe.png` : `dmesg` 의 probe 로그(두 인스턴스)와 `i2cdetect` 의 `UU UU` 가 한 화면에

#### 인스턴스가 둘이면 전역 변수를 쓸 수 없다

**모듈은 하나인데 장치는 둘입니다.** 커널이 `.ko` 를 두 번 적재하는 것이 아니라 같은 코드가 인스턴스별로 실행됩니다. 전역 변수를 쓰면 두 센서가 같은 저장소를 공유해 값이 섞입니다.

```c
struct bh1749_data {
	struct i2c_client *client;
	struct mutex lock;
	u16 red, green, blue, ir;
};

data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
if (!data)
	return -ENOMEM;

data->client = client;
i2c_set_clientdata(client, data);
```

`devm_kzalloc()` 이 디바이스마다 별도 메모리를 할당하고, `i2c_set_clientdata()` 로 client에 연결해 두면 이후 `i2c_get_clientdata()` 로 꺼내 씁니다. probe가 두 번 불리면 메모리도 두 덩어리가 잡혀 서로 간섭하지 않습니다.

`drivers/chan_drv/` 에서는 전역 변수를 쓰고 있으며 이는 개선 대상으로 남겨 두었습니다. 이 드라이버는 처음부터 인스턴스별 구조체로 작성합니다.

#### 남은 단계

| 단계 | 내용 | 상태 |
| --- | --- | --- |
| 0 | 배선 · `i2cdetect` · 제조사 ID 확인 | 완료 |
| 1 | 디바이스 트리 오버레이 (`target = <&i2c1>`, 노드 2개) | 완료 |
| 2 | 최소 `i2c_driver` — probe에서 칩 ID 검사 | 완료 |
| 3 | 디바이스별 구조체 + 초기화 시퀀스 + RGB/IR 읽기 | 진행 중 |
| 4 | IIO 서브시스템 등록 | |
| 5 | (선택) INT 핀 인터럽트 | |

4단계까지 가면 표준 경로가 생깁니다.

```
/sys/bus/iio/devices/iio:device0/in_intensity_red_raw
/sys/bus/iio/devices/iio:device0/in_intensity_green_raw
/sys/bus/iio/devices/iio:device0/in_intensity_blue_raw
/sys/bus/iio/devices/iio:device0/in_intensity_ir_raw
```

6절의 LED 서브시스템과 구조가 동일합니다 — 구조체를 채우고, 콜백을 등록하고, `iio_device_register()` 를 호출합니다. 표준을 따르므로 `iio_info` 같은 기존 도구가 그대로 동작합니다.

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

## 저장소 구성

```
.
├── drivers/chan_drv/        # 플랫폼 드라이버 (GPIO + sysfs + chardev + LED)
├── drivers/bh1749/          # I2C 컬러센서 드라이버 (BH1749NUC)
├── overlays/                # 디바이스 트리 오버레이
├── scripts/build.sh         # 크로스 컴파일 스크립트
├── docs/                    # 검증 사진 및 로그 (README의 <사진> 자리에 삽입)
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
- [ ] 입력 핀 + 인터럽트 — 버튼 입력에 대한 ISR 처리
- [ ] BH1749NUC — IIO 서브시스템 등록 및 RGB/IR 값 읽기
- [ ] 인터럽트 처리 — 센서 INT 핀 또는 버튼 입력에 대한 ISR
