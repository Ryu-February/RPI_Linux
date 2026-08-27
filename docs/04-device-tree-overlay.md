# 디바이스 트리 오버레이 전환 — 완료

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

타깃의 `/boot/firmware/config.txt` 에 `dtoverlay=my-device` 가 등록된 화면(xshell 캡처화면)

<img width="954" alt="타깃의 /boot/firmware/config" src="https://github.com/user-attachments/assets/cbc75684-e513-48ef-8690-7cf7b5da972a" />

`ls /proc/device-tree/my_device/` 출력.

<img width="414" alt="ls /proc/device-tree/my_device/ 출력" src="https://github.com/user-attachments/assets/b830cd08-ef41-473e-98e3-e2c92834f7b6" />

---

[← 3. sysfs + GPIO 제어](03-sysfs-gpio.md) · [목차](../README.md) · [5. 캐릭터 디바이스 →](05-char-device.md)
