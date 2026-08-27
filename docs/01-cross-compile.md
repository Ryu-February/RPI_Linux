# 커널 크로스 컴파일 및 교체 — 완료

x86 WSL2에서 aarch64 커널을 빌드해 RPi4의 순정 커널을 교체했습니다.
`kernel8.img` + `*.dtb` + `/lib/modules/<버전>` 3종 세트를 모두 이식해야 부팅된다는 점을 실패를 통해 확인했습니다.

→ 상세: [TROUBLESHOOTING.md](../TROUBLESHOOTING.md#1-커널-이미지-교체-후-부팅-불가)

WSL(우분투) 크로스 컴파일 진행 화면

<img width="982" alt="WSL(우분투) 크로스 컴파일 진행 화면" src="https://github.com/user-attachments/assets/43e430b6-474c-458a-a50c-370609a3ac4c" />

타깃에서 `uname -a` 출력. 직접 빌드한 `6.12.28-v8+` 커널로 부팅된 것을 보여주는 화면

<img width="618" alt="타깃에서 uname -a 출력" src="https://github.com/user-attachments/assets/3c232923-1663-41cb-8166-db0fd77d0966" />

---

[목차](../README.md) · [2. 플랫폼 드라이버 + DTS 매칭 →](02-platform-driver.md)
