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

## 다음

- [ ] `irq_count` 를 sysfs로 노출 — spinlock이 실제로 필요한 상황 구현
- [ ] top half / bottom half 분리 — 인터럽트 컨텍스트 제약 직접 확인

---

[← 7. I2C 컬러센서 (BH1749NUC)](07-i2c-bh1749.md) · [목차](../README.md)
