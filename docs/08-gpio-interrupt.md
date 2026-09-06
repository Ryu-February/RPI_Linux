# GPIO 인터럽트 — 스위치 입력 ISR

앞 단계까지의 드라이버는 모두 **유저가 요청할 때만** 동작합니다. `cat` 하면 읽고 `echo` 하면 씁니다. 하드웨어가 먼저 신호를 보내는 경우를 다루지 않았습니다.

## 왜 센서 INT 핀이 아닌 스위치인가

원래 계획은 BH1749의 INT 핀을 사용하는 것이었습니다. 측정값이 임계치를 벗어나면 INT가 떨어지고, 이를 GPIO 인터럽트로 받아 IIO 이벤트로 올리는 구조입니다.

**그러나 사용 중인 보드에 INT 핀이 외부로 인출되어 있지 않았습니다.** 패키지 안쪽이라 인출도 어렵습니다.

대신 GPIO에 스위치를 연결해 인터럽트를 발생시켰습니다. 커널 입장에서 신호원이 센서인지 스위치인지는 구분되지 않으므로, 학습 대상은 대부분 동일합니다.

| | 센서 INT 핀 | GPIO 스위치 |
| --- | --- | --- |
| `gpiod_to_irq()` | 동일 | 동일 |
| `devm_request_threaded_irq()` | 동일 | 동일 |
| ISR 컨텍스트 제약 | 동일 | 동일 |
| top half / bottom half | 동일 | 동일 |
| 동시성 (spinlock) | 동일 | 동일 |
| 디바운스 | 불필요 | **추가로 다룸** |
| IIO 이벤트 연결 | 가능 | 불가 |

## 배선 방식을 측정으로 결정

보유한 스위치가 풀업인지 풀다운인지 알 수 없어, 드라이버를 작성하기 전에 실측했습니다. I2C에서 `i2cget` 으로 통신을 먼저 확인한 것과 같은 순서입니다.

```bash
$ gpiodetect
gpiochip0 [pinctrl-bcm2711] (58 lines)

$ gpioget -c gpiochip0 27
"27"=inactive     # 누르지 않은 상태 → LOW
"27"=active       # 누른 상태 → HIGH
```

**평소 LOW, 누르면 HIGH** 이므로 풀다운 방식입니다. 따라서 플래그는 ACTIVE_HIGH, 트리거는 상승 엣지가 됩니다.

| 구성 | 배선 | `brcm,pull` | 플래그 | 엣지 |
| --- | --- | --- | --- | --- |
| 풀업 | 스위치 → GND | `2` | `1` (ACTIVE_LOW) | FALLING |
| **풀다운 (채택)** | 스위치 → 3.3V | `0` 또는 `1` | `0` (ACTIVE_HIGH) | RISING |
| 없음 | — | `0` | — | 사용 금지 |

풀이 없으면 핀이 플로팅 상태가 되어, 노이즈만으로 인터럽트가 계속 발생합니다. 입력 핀에는 항상 둘 중 하나를 겁니다. 사용한 스위치 모듈에는 자체 풀다운이 있어 내부 풀은 켜지 않았습니다.

`gpiod` 2.x 부터 CLI 문법이 바뀌어 칩을 `-c` 로 지정해야 합니다.

## 오버레이 — pinctrl과 GPIO 추가

```dts
fragment@0 {
	target = <&gpio>;
	__overlay__ {
		my_button_pins: my_button_pins {
			brcm,pins = <27>;
			brcm,function = <0>;	/* input */
			brcm,pull = <0>;
		};
	};
};

fragment@1 {
	target-path = "/";
	__overlay__ {
		my_device {
			...
			gpios = <&gpio 17 0>;
			button-gpios = <&gpio 27 0>;
			pinctrl-names = "default";
			pinctrl-0 = <&my_button_pins>;
		};
	};
};
```

### 벤더 전용 속성

핀의 풀업/풀다운 설정은 **SoC 벤더마다 속성 이름이 다릅니다.** 다른 SoC는 `bias-pull-up` 같은 표준 속성을 쓰지만, 라즈베리파이는 자체 pinctrl 드라이버라 `brcm,` 접두사 형식을 사용합니다.

| `brcm,pull` | 의미 |
| --- | --- |
| `0` | 없음 |
| `1` | 풀다운 |
| `2` | 풀업 |

참고로 RPi의 전원 인가 시 기본값은 GPIO 0~8이 풀업, 9~27이 풀다운, 28~45가 없음입니다.

### 이름을 붙인 GPIO

기존 LED는 `gpios`, 스위치는 `button-gpios` 로 구분했습니다.

```c
devm_gpiod_get(dev, NULL,     GPIOD_OUT_LOW);  /* gpios        → LED */
devm_gpiod_get(dev, "button", GPIOD_IN);       /* button-gpios → 스위치 */
```

이름을 주면 `<이름>-gpios` 를 찾습니다. GPIO 절에서 `NULL` 을 사용한 이유가 여기서 드러납니다.

## IRQ 등록

```c
my_button = devm_gpiod_get(dev, "button", GPIOD_IN);
if (IS_ERR(my_button))
	return PTR_ERR(my_button);

my_irq = gpiod_to_irq(my_button);
if (my_irq < 0)
	return my_irq;

ret = devm_request_threaded_irq(dev, my_irq, NULL, chan_button_isr,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				"chan_button", dev);
```

`gpiod_to_irq()` 는 GPIO 디스크립터를 인터럽트 번호로 변환합니다. 모든 GPIO가 인터럽트를 지원하지는 않으므로 실패할 수 있습니다.

### threaded IRQ

| 인자 | 값 | 의미 |
| --- | --- | --- |
| `handler` | **`NULL`** | top half 없음 |
| `thread_fn` | `chan_button_isr` | **커널 스레드에서** 실행 |
| `flags` | `IRQF_TRIGGER_RISING` | 상승 엣지에 반응 |
| | `IRQF_ONESHOT` | 스레드가 끝날 때까지 인터럽트 마스크 |
| `name` | `"chan_button"` | `/proc/interrupts` 표시 이름 |
| `dev_id` | `dev` | 핸들러에 전달될 값 |

`handler` 를 `NULL` 로 두면 전체가 스레드 컨텍스트에서 실행되므로 `dev_info()` 를 호출하고 잠들 수도 있습니다.

이 자리를 함수로 채우면 그것은 **진짜 인터럽트 컨텍스트**이며, sleep·mutex·`copy_to_user` 가 모두 금지됩니다. 이 제약이 인터럽트 처리의 핵심입니다.

### 등록 확인

```
my_dev_drv my_device: GPIO acquire success
my_dev_drv my_device: button irq 59 registered
```

```bash
$ cat /proc/interrupts | grep chan_button
 59:  0  0  0  0  pinctrl-bcm2835  27 Edge  chan_button
```

커널이 관리하는 인터럽트 목록에 직접 지정한 이름이 등록됩니다. 앞의 숫자 넷은 CPU별 발생 횟수로, 스위치를 누를 때마다 증가합니다.

**<<img width="866" height="168" alt="image" src="https://github.com/user-attachments/assets/cb82ddfd-4ef1-4bd2-a267-4a2877dc6db5" />
>** — `dmesg` 의 `button irq 59 registered` 와 `/proc/interrupts` 출력

## 채터링

스위치를 한 번 눌렀는데 로그가 수십 줄 출력되었습니다.

```
button irq #1 (led=1)
button irq #2 (led=0)
button irq #3 (led=1)
...
```

기계식 접점이 붙는 순간 수 ms 동안 미세하게 튀면서 엣지가 여러 번 발생합니다. LED도 여러 번 토글되어 최종 상태를 예측할 수 없습니다.

**<<img width="866" height="168" alt="image" src="https://github.com/user-attachments/assets/0e4494ff-27ca-49f1-821d-c65b1b6ed11e" />
>** — 한 번 눌렀는데 로그가 여러 줄 출력된 화면

## 소프트웨어 디바운스

마지막으로 처리한 시각을 기억해 두고, 일정 시간 안에 다시 들어온 인터럽트는 버립니다.

```c
#define CHAN_BOUNCE_MS	200

static unsigned int irq_count;
static unsigned int bounce_count;
static unsigned long last_jiffies;
static DEFINE_SPINLOCK(irq_lock);

static irqreturn_t chan_button_isr(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	unsigned long flags;
	unsigned int count, bounces;
	bool ignore = false;

	spin_lock_irqsave(&irq_lock, flags);

	if (last_jiffies &&
	    time_before(jiffies,
			last_jiffies + msecs_to_jiffies(CHAN_BOUNCE_MS))) {
		bounce_count++;
		ignore = true;
	} else {
		last_jiffies = jiffies;
		irq_count++;
	}

	count = irq_count;
	bounces = bounce_count;

	spin_unlock_irqrestore(&irq_lock, flags);

	if (ignore)
		return IRQ_HANDLED;

	my_value = !my_value;
	if (my_gpio)
		gpiod_set_value(my_gpio, my_value ? 1 : 0);

	dev_info(dev, "button irq #%u (led=%u, bounced=%u)\n",
		 count, my_value, bounces);

	return IRQ_HANDLED;
}
```

### `time_before()` 를 쓰는 이유

`jiffies` 는 부팅 후 경과한 타이머 틱이며 언젠가 오버플로우됩니다. `<` 로 직접 비교하면 그 시점에 오작동하므로, 커널에서 `jiffies` 비교는 항상 이 매크로를 사용합니다.

### spinlock

`irq_count` 를 ISR과 (이후) sysfs 읽기가 함께 접근합니다. 락 없이 증가시키면 값이 깨집니다.

`_irqsave` 는 락을 잡는 동안 로컬 CPU의 인터럽트를 비활성화합니다. 인터럽트 컨텍스트에서도 안전하려면 이 형태여야 합니다.

### 하드웨어 디바운스

```c
gpiod_set_debounce(my_button, 200000);	/* 마이크로초 */
```

**bcm2835 pinctrl은 이를 지원하지 않아 `-ENOTSUPP` 를 반환합니다.** 일부 SoC는 하드웨어로 디바운스를 처리하지만 라즈베리파이는 제공하지 않으므로 소프트웨어로 구현했습니다.

### 검증

```
button irq #1 (led=0, bounced=0)
button irq #2 (led=1, bounced=2)
button irq #3 (led=0, bounced=3)
button irq #4 (led=1, bounced=3)
```

**LED가 0 → 1 → 0 → 1 로 규칙적으로 토글됩니다.** 디바운스 전에는 한 번 눌러도 여러 번 뒤집혀 최종 상태가 불규칙했습니다. `bounced` 는 걸러낸 개수입니다.

임계값은 스위치 특성에 따라 조정합니다. 20~50ms는 빠른 연타가 가능하지만 채터링이 일부 통과하고, 300ms 이상은 확실히 걸러지지만 반응이 둔해집니다.

**<사진>** — 디바운스 적용 후 한 번 누르면 한 줄만 출력되는 화면

## 카운터를 sysfs로 노출

`dmesg` 를 봐야만 알 수 있던 카운터를 파일로 뺐습니다.

```c
static ssize_t irq_count_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	unsigned long flags;
	unsigned int count;

	spin_lock_irqsave(&irq_lock, flags);
	count = irq_count;
	spin_unlock_irqrestore(&irq_lock, flags);

	return sysfs_emit(buf, "%u\n", count);
}
static DEVICE_ATTR_RO(irq_count);
```

`bounce_count` 도 같은 형태로 하나 더 만들었습니다. **값 하나에 파일 하나**가 sysfs 관례이므로, 두 값을 한 파일에 넣어 파싱하게 만들지 않았습니다.

### 여기서 락이 실제로 필요해진다

이전까지는 락을 걸어 두었지만 경쟁 상대가 없었습니다. 카운터를 만지는 주체가 ISR 하나뿐이었기 때문입니다.

```
스위치 입력 → ISR (커널 스레드)     ─┐
                                     ├─ irq_count 동시 접근
cat irq_count → show (프로세스)     ─┘
```

`unsigned int` 읽기가 대개 원자적으로 처리되기는 하지만, **그것은 아키텍처와 컴파일러에 의존하는 가정**이므로 커널 코드에서는 그렇게 작성하지 않습니다. 두 카운터를 함께 읽어야 하는 경우라면 문제가 더 분명해집니다 — 중간에 ISR이 끼어들면 서로 맞지 않는 조합이 나옵니다.

### 에러 처리 계단

sysfs 파일이 늘면서 되감기 단계도 늘었습니다.

```c
err_bounce:
	device_remove_file(dev, &dev_attr_bounce_count);
err_irqcount:
	device_remove_file(dev, &dev_attr_irq_count);
err_sysfs:
	device_remove_file(dev, &dev_attr_value);
	return ret;
```

**계단을 추가할 때는 기존 `goto` 목적지도 함께 내려야 합니다.** 이 과정에서 `irq_count` 생성 실패 시 `err_bounce` 로 점프하도록 잘못 작성한 버그가 있었습니다. 아직 만들지 않은 파일을 제거하려 드는 경로입니다.

실패 지점은 **자기 단계를 건너뛰고 그 앞부터** 되감아야 한다는 규칙을 다시 확인했습니다. 같은 실수를 이 드라이버에서 세 번째 반복한 것이라, 계단이 깊어지면 속성 그룹(`.dev_groups`)으로 전환하는 편이 안전합니다. BH1749 드라이버에서 사용한 방식입니다.

### 검증

```bash
$ ls /sys/devices/platform/my_device/
bounce_count  driver  irq_count  leds  of_node  power  subsystem  uevent  value
supplier:platform:fe200000.gpio

$ grep . /sys/devices/platform/my_device/{irq_count,bounce_count}
/sys/devices/platform/my_device/irq_count:11
/sys/devices/platform/my_device/bounce_count:20
```

`dmesg` 없이 스위치 동작을 확인할 수 있습니다. 한 번 누를 때 평균 2개 정도가 걸러지고 있습니다.

### 커널 카운터와 대조

```bash
cat /proc/interrupts | grep chan_button
```

`/proc/interrupts` 는 인터럽트 라인이 만들어진 뒤의 **누적값**이라 모듈을 다시 적재해도 초기화되지 않습니다. 따라서 절대값이 아니라 **증분**을 비교해야 합니다.

모듈 적재 직후 값을 기록하고 스위치를 N번 누른 뒤, `irq_count + bounce_count` 와 `/proc/interrupts` 증분이 일치하는지 확인합니다. 다만 `IRQF_ONESHOT` 으로 스레드 실행 중 인터럽트가 마스크되므로 완전히 같지 않을 수 있습니다.

### sysfs에 함께 생긴 것들

```
leds                              LED 서브시스템이 만든 링크
supplier:platform:fe200000.gpio   GPIO 컨트롤러 의존성
```

두 번째는 오버레이에서 `pinctrl-0` 을 지정한 결과로 커널이 기록한 것입니다. 이 의존성 정보로 probe 순서와 서스펜드/리줌 순서가 결정됩니다.

## 락 범위를 어디까지 잡을 것인가

카운터를 sysfs로 노출하고 나서 코드를 다시 훑다가, `my_value` 갱신과 GPIO 출력이 락 없이 연달아 수행되고 있는 것을 발견했습니다.

```c
my_value = tmp;                              /* 변수 */
gpiod_set_value(my_gpio, my_value ? 1 : 0);  /* 하드웨어 */
```

각각은 원자적이지만 **둘을 묶은 것은 아닙니다.** 사이에 다른 컨텍스트가 끼어들면 변수 값과 실제 LED 상태가 어긋난 채로 남습니다.

### 컴파일러가 창을 만든다

소스에는 두 번째 줄에서 `my_value` 를 읽는다고 썼지만, 역어셈블해보니 메모리를 다시 읽지 않습니다.

```
99c:  str  w2, [x19]         ; my_value = tmp
9a0:  ldr  x0, [x0, #120]    ; my_gpio 로드
9a4:  cbz  x0, 9b8           ; if (my_gpio)
9a8:  cmp  w2, #0x0          ; 메모리 재로드 없음. w2 재사용
9b0:  bl   gpiod_set_value
9b4:  ldr  w2, [x19]         ; 외부 함수 호출 뒤라 여기선 재로드
```

즉 실제로 실행되는 코드는 `gpiod_set_value(my_gpio, tmp ? 1 : 0)` 입니다. 그 사이 ISR이 `my_value` 를 바꿔도 하드웨어에는 반영되지 않습니다.

9b4에서는 다시 읽습니다. 외부 함수 호출이 전역을 바꿨을 수 있으므로 컴파일러가 재로드한 것입니다.

### 재현

컴파일러가 하는 일(레지스터 캐싱)을 소스에 그대로 옮겨 적고, 그 사이에 지연을 넣었습니다. 없는 버그를 만든 것이 아니라 나노초 창을 5초로 벌린 것입니다.

```c
my_value = tmp;
snapshot = my_value;
if (my_gpio) {
	msleep(5000);
	gpiod_set_value(my_gpio, snapshot ? 1 : 0);
}
```

`store` 는 프로세스 컨텍스트이므로 `msleep()` 을 부를 수 있습니다. ISR(`handler` 슬롯)이었다면 커널이 죽습니다.

### 지연을 넣으면 버그가 사라진다

처음에는 `snapshot` 없이 이렇게 썼습니다.

```c
my_value = tmp;
if (my_gpio) {
	msleep(5000);
	gpiod_set_value(my_gpio, my_value ? 1 : 0);   /* snapshot 이 아니라 my_value */
}
```

이 버전은 **재현되지 않습니다.** 창 안에서 스위치를 눌러도 값과 LED가 일치합니다.

`msleep()` 이 외부 함수 호출이라 컴파일러가 그 뒤에서 `my_value` 를 **메모리에서 다시 읽기** 때문입니다. ISR이 바꿔놓은 값을 그대로 읽어 반영하므로 결과가 맞아떨어집니다.

즉 **관찰하려고 넣은 지연이 버그를 고쳐버립니다.** 원본 코드에는 `msleep` 이 없어 재로드가 일어나지 않고, 역어셈블의 `cmp w2, #0x0` 이 그 증거입니다.

`snapshot` 은 이 문제를 우회하려고 둔 장치입니다. 컴파일러가 레지스터에 담아두던 것을 소스에 명시적으로 옮겨 적은 것이고, 그래야 원본과 같은 조건이 됩니다.

> 관찰 수단이 관찰 대상을 바꾸는 경우입니다. 최적화가 개입하는 동시성 버그를 다룰 때 흔히 겪는 함정이고, `printk` 를 넣었더니 증상이 사라지는 상황도 같은 계열입니다.

```bash
echo 1 | sudo tee /sys/devices/platform/my_device/value   # LED 켜짐
echo 0 | sudo tee /sys/devices/platform/my_device/value   # 5초 멈춤
# 그 사이에 스위치를 누른다
```

```bash
$ cat /sys/devices/platform/my_device/value
1
```
![Uploading image.png…]()

값은 1인데 LED는 꺼져 있습니다. `store` 가 5초 전 스냅샷으로 GPIO를 덮어썼기 때문입니다.

핀 상태로도 교차 확인했습니다.

```bash
$ cat /sys/devices/platform/my_device/value
1
$ sudo cat /sys/kernel/debug/gpio | grep GPIO17
 gpio-529 (GPIO17   |my_device   ) out lo
```

**<<img width="1745" height="173" alt="image" src="https://github.com/user-attachments/assets/55ef400d-1303-491f-a25d-9687311cfd14" />
>** — 변수는 1, 핀은 `out lo`

### 락을 걸 곳

`my_value` 를 만지는 여덟 곳을 컨텍스트별로 분류했습니다.

| 함수 | 컨텍스트 | 하는 일 | 락 |
|---|---|---|---|
| `value_show` | 프로세스 | 읽기 1개 | `READ_ONCE` |
| `value_store` | 프로세스 | 변수 + GPIO | 필요 |
| `chan_read` | 프로세스 | 읽기 1개 | `READ_ONCE` |
| `chan_write` | 프로세스 | 변수 + GPIO | 필요 |
| `chan_led_set` | 아토믹 가능 | 변수 + GPIO | 필요 |
| `chan_led_get` | 프로세스 | 읽기 1개 | `READ_ONCE` |
| `chan_button_isr` | 커널 스레드 | 변수 + GPIO | 필요 |
| `my_probe` | 프로세스 | 쓰기 1개 | 불필요 |

판단 기준은 **값 하나를 읽고 쓰느냐, 여러 개를 일관되게 유지해야 하느냐** 입니다. aarch64에서 정렬된 32비트 읽기는 하드웨어가 원자적으로 처리하므로 읽기에는 락이 필요 없고, 컴파일러 캐싱만 `READ_ONCE` 로 막으면 됩니다.

`my_probe` 가 예외인 이유는 그 시점에 sysfs·chardev·LED가 아직 등록되지 않아 경쟁 상대가 존재하지 않기 때문입니다. 준비를 마치고 마지막에 등록하는 순서 덕분입니다.

### mutex 가 아닌 이유

`chan_led_set` 은 `led_classdev.brightness_set` 슬롯에 등록돼 있고, LED 코어는 이 슬롯을 잘 수 없는 것으로 취급합니다. `heartbeat` 같은 트리거가 타이머(소프트IRQ)에서 호출하기 때문입니다.

```bash
echo heartbeat | sudo tee /sys/class/leds/chan:led/trigger
```

따라서 mutex 는 후보에서 탈락하고 `spin_lock_irqsave` 를 씁니다.

`gpiod_set_value()` 를 스핀락 안에 넣어도 되는지도 확인이 필요했습니다. `drivers/gpio/gpiolib.c` 주석에 "can be called from contexts where we cannot sleep" 이라고 명시돼 있어 안전합니다. 짝이 되는 `gpiod_set_value_cansleep()` 은 `might_sleep()` 을 부르므로 쓸 수 없습니다. I2C/SPI GPIO 확장칩에 LED를 달았다면 후자를 써야 하고, 그 경우 설계를 바꿔야 했습니다.

### 이 드라이버에서의 심각도

낮습니다. ISR이 변수와 GPIO를 함께 갱신하므로 다음 스위치 입력에서 상태가 다시 맞춰집니다. 실제로 재현 후 두세 번 더 누르니 정상으로 돌아왔습니다.

다만 변수가 다른 로직의 판단 근거가 되는 경우에는 자가 치유되지 않습니다.

```c
if (my_value)
	start_transfer();
```

패턴은 위험하고 이 인스턴스는 경미하다, 가 정확한 평가입니다.

### 트리거로는 관찰되지 않았다

처음에는 `heartbeat` 를 켜고 스위치를 눌러 관찰하려 했으나 실패했습니다. 트리거가 수십 ms마다 GPIO를 갱신하므로 ISR의 토글이 즉시 덮어써집니다. 경쟁 조건이 아니라 소유권 충돌입니다.

여기서 별개의 설계 문제가 드러났습니다. `chan_button_isr` 이 LED 서브시스템을 건너뛰고 GPIO를 직접 쓰고 있어, 트리거가 도는 동안 코어와 ISR이 서로 모른 채 같은 핀을 다툽니다. `led_set_brightness()` 를 쓰도록 바꾸는 것이 맞습니다.

## 다음

- [ ] 락 4곳 적용 후 재검증
- [ ] `chan_button_isr` 이 `led_set_brightness()` 를 쓰도록 변경
- [ ] 읽기 3곳에 `READ_ONCE` 적용
- [ ] top half / bottom half 분리 — 인터럽트 컨텍스트 제약 직접 확인
- [ ] 에러 처리를 `.dev_groups` 속성 그룹으로 전환해 계단 제거

---

[← 7. I2C 컬러센서 (BH1749NUC)](07-i2c-bh1749.md) · [목차](../README.md)
