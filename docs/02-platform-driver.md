# 플랫폼 드라이버 + DTS 매칭 — 완료

디바이스 트리에 가상 노드를 선언하고, `of_device_id` 의 `compatible` 문자열로 드라이버를 매칭시켜 `probe()` 를 호출시켰습니다.

```c
static const struct of_device_id my_match[] = {
	{ .compatible = "chan,my-device" },
	{ }
};
```

DTS에서 `my-number`, `my-string` 프로퍼티를 읽어 드라이버가 하드웨어 정보를 **소스에 하드코딩하지 않고** 받아쓰는 구조를 구현했습니다.

---

[← 1. 커널 크로스 컴파일 및 교체](01-cross-compile.md) · [목차](../README.md) · [3. sysfs + GPIO 제어 →](03-sysfs-gpio.md)
