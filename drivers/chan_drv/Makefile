obj-m += chan_drv.o

# 커널 소스 트리와 빌드 출력 경로. 환경에 맞게 덮어쓸 수 있습니다.
#   make KDIR=/path/to/linux OUT=/path/to/out
KDIR ?= /project/25-linuxKernel2/linux
OUT  ?= /project/25-linuxKernel2/out

# M=$(CURDIR) 를 쓰는 이유: $(PWD) 는 셸이 넘겨주는 환경변수라
# sudo 로 실행하면 사라져 M= 이 비고, 커널 전체가 빌드됩니다.
all:
	$(MAKE) -C $(KDIR) O=$(OUT) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) O=$(OUT) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$(CURDIR) clean
