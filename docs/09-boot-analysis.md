# 9. 부팅 시퀀스 분석과 부팅 시간 단축

드라이버 안쪽만 파다 보니 **시스템이 어떻게 올라오는지**를 모르고 있었습니다. 전원 인가부터 로그인 프롬프트까지를 구간별로 나누고, 측정한 뒤 줄이는 것까지 해봤습니다.

---

## 부팅 5단계

```
① SoC 부트 ROM      칩 안에 구워진 코드. 수정 불가
② EEPROM 부트로더    보드의 SPI 플래시. 부팅 매체 선택
③ start4.elf        GPU 펌웨어. config.txt / kernel8.img / dtb 로드
④ 리눅스 커널        DT 파싱 → initcall → 루트 마운트
⑤ systemd           서비스 기동 → 로그인
```

### 라즈베리파이는 GPU 가 부팅한다

보통의 ARM 보드는 ARM 코어가 먼저 깨어나 U-Boot 을 돌리지만, RPi 는 **VideoCore(GPU) 가 부팅하고 마지막에 ARM 을 깨웁니다.** ①②③이 전부 브로드컴 바이너리라 **이 보드로는 부트로더를 배울 수 없습니다.**

근거는 `/proc/cmdline` 에 있습니다.

```
coherent_pool=1M 8250.nr_uarts=1 ... vc_mem.mem_size=0x40000000  console=tty1 root=PARTUUID=...
└──────── start4.elf 가 붙인 것 ────────┘ └──── cmdline.txt ────┘
```

앞부분은 `cmdline.txt` 에 쓴 적이 없습니다. GPU 가 하드웨어 상태(메모리 분할, UART 개수)를 알아내 커널에게 넘겨준 것입니다.

### 오버레이는 커널이 아니라 GPU 가 합친다

```bash
$ dtc -I dtb -O dts /boot/firmware/bcm2711-rpi-4-b.dtb | grep my_device
(없음)

$ dtc -I fs -O dts /proc/device-tree | grep -A3 my_device
my_device {
	compatible = "chan,my-device";
	my-number = <0x2a>;
```

`.dtb` 파일에는 없고 `/proc/device-tree` 에는 있습니다. **커널은 이미 합쳐진 결과만 받고, 오버레이가 있었는지도 모릅니다.** `.dtbo` 를 `/boot/firmware/overlays/` 에 두어야 했던 이유입니다.

---

## 측정 도구

### 커널 구간 — `initcall_debug`

`cmdline.txt` 에 추가합니다. 한 줄이어야 하고, `log_buf_len` 을 같이 주지 않으면 800줄 넘는 출력이 기본 버퍼를 넘겨 초반 로그가 밀립니다.

```
initcall_debug log_buf_len=4M
```

```bash
sudo dmesg | sed -n 's/.*initcall \([^ ]*\) returned 0 after \([0-9]*\) usecs.*/\2 \1/p' | sort -rn | head -20
```

### 유저스페이스 구간 — `systemd-analyze`

```bash
systemd-analyze
systemd-analyze blame | head -15
systemd-analyze critical-chain
```

`critical-chain` 의 `@` 는 **활성화된 시점**, `+` 는 **걸린 시간**입니다. `blame` 은 병렬로 도는 것들을 각각 재는 것이라 단순 합계가 총합이 아니고, **무엇이 무엇을 기다렸는지**는 `critical-chain` 만 보여줍니다.

> `initcall_debug` 출력의 `@ 397` 은 시각이 아니라 **프로세스 ID** 입니다. 기호가 같지만 뜻이 전혀 다릅니다.

---

## 측정 — 개선 전

```
Startup finished in 2.091s (kernel) + 21.218s (userspace) = 23.310s
```

**커널은 이미 빨랐습니다.** 유저스페이스가 10배입니다. 재보기 전에는 커널을 최적화해야 한다고 생각했는데, 측정하지 않았으면 엉뚱한 데를 팠을 것입니다.

**<사진 1<img width="585" height="391" alt="image" src="https://github.com/user-attachments/assets/976f5613-6244-4875-88df-6c02e27e87a7" />
>** — 개선 전 `systemd-analyze` / `blame`

### critical-chain

```
multi-user.target @21.020s
└─smbd.service @20.575s +443ms
  └─winbind.service @20.123s +449ms
    └─nmbd.service @19.248s +871ms
      └─network-online.target @19.242s
        └─NetworkManager-wait-online.service @13.259s +5.981s
          └─NetworkManager.service @7.491s +5.761s
            └─dbus.service @7.318s +163ms
              └─cloud-init-main.service @4.580s +2.141s
```

**21초 중 12초가 네트워크**고, Samba 가 그 뒤에서 2초를 더 씁니다.

### 커널 구간의 느린 항목

```
405ms  init_blk_tracer         ftrace 블록 I/O 추적기. 평소엔 안 씀
197ms  bcmgenet_driver_init    이더넷
114ms  brcm_pcie_driver_init   PCIe
107ms  xhci_pci_init           USB 3.0
 36ms  deferred_probe_initcall
```

RPi4 는 USB 가 PCIe 뒤에 달려 있어 순서가 강제됩니다.

---

## 빌트인과 모듈을 로그에서 구분하기

```
[ 0.510] calling  bcm2835_pinctrl_driver_init+0x0/0x40 @ 1
[ 5.753] calling  bcm2835_i2c_driver_init+0x0/0xff8 [i2c_bcm2835] @ 397
```

| | 빌트인 | 모듈 |
|---|---|---|
| 표기 | 괄호 없음 | `[i2c_bcm2835]` |
| 시각 | 0.5초 | 5.7초 |
| `@ N` | `@ 1` (커널) | `@ 397` (modprobe) |

**I2C 컨트롤러 드라이버는 커널 initcall 이 아니라 udev 가 5.7초에 올린 것**입니다. `MODULE_DEVICE_TABLE` → `modules.alias` → udev 자동 로드의 실물이고, 커널 부팅이 끝난 뒤에도 드라이버는 계속 올라온다는 뜻입니다.

---

## 조치

```bash
sudo systemctl disable NetworkManager-wait-online.service
sudo touch /etc/cloud/cloud-init.disabled
sudo systemctl disable winbind.service samba-ad-dc.service
```

| 대상 | 이유 | 잃는 것 |
|---|---|---|
| `NetworkManager-wait-online` | 네트워크가 붙을 때까지 부팅을 멈추는 **신호등**. 네트워크 자체는 `NetworkManager` 가 담당 | 없음 |
| `cloud-init` | rpi-imager 가 첫 부팅 설정용으로 넣은 것. 역할이 끝남 | 없음 |
| `winbind` / `samba-ad-dc` | 윈도우 AD 연동 / Samba 를 도메인 컨트롤러로 돌리는 기능 | AD 기능 (사용 안 함) |

**네 개 모두 유저스페이스 서비스이고 커널 모듈이 아닙니다.** `smbd` / `nmbd` 는 파일 공유가 쓰므로 남겼습니다.

---

## 측정 — 개선 후

```
Startup finished in 2.071s (kernel) + 15.309s (userspace) = 17.381s
```

**<사진 2<img width="493" height="361" alt="image" src="https://github.com/user-attachments/assets/bbae4800-d9a6-4b4a-8374-c12261974118" />
>** — 개선 후 `systemd-analyze` / `critical-chain`

| | 전 | 후 |
|---|---|---|
| 커널 | 2.091s | 2.071s |
| 유저스페이스 | 21.218s | 15.309s |
| **합계** | **23.310s** | **17.381s** |

**25% 단축.** 커널은 손대지 않았습니다.

### 개별 항목은 늘 수도 있다

`nmbd` 가 871ms 에서 3.740s 로 **늘었습니다.** 이전에는 `network-online.target` 뒤에서 시작해 네트워크가 이미 준비된 상태였지만, 장벽을 없애니 먼저 시작했다가 자기가 기다리게 된 것입니다.

**총합은 줄었는데 개별 항목은 늘어난 경우**로, `blame` 만 보고 판단하면 안 되는 이유입니다.

### 남은 것

```
6.420s  NetworkManager      Wi-Fi 스캔 + WPA2 핸드셰이크 + DHCP
3.740s  nmbd
1.156s  rpi-eeprom-update
```

무선 연결에 드는 물리적 시간이 바닥입니다. `rpi-eeprom-update`(매 부팅 부트로더 갱신 확인)와 `nmbd`(NetBIOS 이름 알림, IP 직접 접속 시 불필요)를 더 끄면 13초 안팎이 예상됩니다.

---

## 배운 것

**재보기 전에 고치면 엉뚱한 데를 고친다.** 커널 2초 / 유저스페이스 21초 — 문제는 커널이 아니었습니다.

**측정 → 원인 지목 → 조치 → 재측정.** 부팅 시간 단축 요구가 들어왔을 때 하는 일이 이 네 단계입니다. `initcall_debug` 로 커널을, `systemd-analyze` 로 유저스페이스를 나눠 재면 부팅 전체가 분해됩니다.

**RPi 로는 부트로더를 못 배운다.** ①②③이 폐쇄 바이너리입니다. 이 구간을 보려면 U-Boot 이 열린 보드가 필요합니다.

---

## 다음

- [ ] `rpi-eeprom-update` / `nmbd` 비활성화 후 재측정
- [ ] `CONFIG_BLK_DEV_IO_TRACE=n` 으로 0.4초 단축 — 커널 재빌드 필요
- [ ] `chan_drv` 를 `/lib/modules` 에 설치하고 `depmod` → udev 자동 로드 확인
- [ ] initramfs 유무와 역할 정리

---

[← 8. GPIO 인터럽트](08-gpio-interrupt.md) · [목차](../README.md)
