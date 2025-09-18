TOP=$(abspath ..)
TARGET=cvmvhd

.PHONY: dump

CFLAGS= -Werror -Wall
INCLUDES = -I$(TOP)
LDFLAGS += -L$(TOP)/common -lcvmbootcommon -L$(TOP)/utils -lcvmbootutils

all:
	gcc $(CFLAGS) $(INCLUDES) -o $(TARGET) main.c $(LDFLAGS)

clean:
	rm -f $(TARGET) sample.vhd.gz sample.vhd image
	rm -f test-fixed.vhd expanded.vhd compacted.vhd final.vhd original.img final.img

install:
	sudo cp cvmvhd /usr/bin/cvmvhd

uninstall:
	sudo rm -rf /usr/bin/cvmvhd

distclean: clean

dump:
	./$(TARGET) dump image

append: sample.vhd
	cp --sparse=auto sample.vhd image
	./$(TARGET) append image
	./$(TARGET) dump image | grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Expected disk_type=2 after append" && exit 1)
	@echo "✅ PASS: VHD footer appended correctly"
	truncate --size=-512 image
	./$(TARGET) append image
	./$(TARGET) dump image | grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Expected disk_type=2 after re-append" && exit 1)
	@echo "✅ PASS: VHD footer replaced correctly"

remove:
	./$(TARGET) remove image
	@# Verify footer was removed by checking if dump fails (no VHD footer)
	@if ./$(TARGET) dump image 2>/dev/null; then echo "❌ FAIL: VHD footer still present after remove" && exit 1; else echo "✅ PASS: VHD footer removed successfully"; fi

sample.vhd:
	cp ../share/cvmboot/sample.vhd.gz sample.vhd.gz
	gzip -d sample.vhd.gz
	./cvmvhd dump sample.vhd| grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Expected disk_type=2 for fixed VHD" && exit 1)

tests:
	$(MAKE) sample.vhd
	$(MAKE) append
	$(MAKE) remove
	$(MAKE) expand-compact-tests

expand-compact-tests: test-fixed.vhd
	@echo "=== Step 1: Create and verify fixed VHD ==="
	./cvmvhd dump test-fixed.vhd | grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Expected disk_type=2 for fixed VHD" && exit 1)
	@echo "✅ PASS: Created fixed VHD (disk_type=2)"
	@echo "=== Step 2: Extract baseline raw image ==="
	./cvmvhd extract test-fixed.vhd original.img
	@echo "✅ PASS: Extracted baseline raw image"
	@echo "=== Step 3: Expand fixed VHD (should copy) ==="
	./cvmvhd expand test-fixed.vhd expanded.vhd
	./cvmvhd dump expanded.vhd | grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Expected disk_type=2 after expand" && exit 1)
	@echo "✅ PASS: Expanded VHD remains fixed (disk_type=2)"
	@echo "=== Step 4: Compact to dynamic VHD ==="
	./cvmvhd compact expanded.vhd compacted.vhd
	./cvmvhd dump compacted.vhd | grep "disk_type=3" > /dev/null || (echo "❌ FAIL: Expected disk_type=3 for dynamic VHD" && exit 1)
	@echo "✅ PASS: Compacted to dynamic VHD (disk_type=3)"
	@echo "=== Step 5: Expand dynamic back to fixed ==="
	./cvmvhd expand compacted.vhd final.vhd
	./cvmvhd dump final.vhd | grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Expected disk_type=2 after expand" && exit 1)
	@echo "✅ PASS: Expanded back to fixed VHD (disk_type=2)"
	@echo "=== Step 6: Verify data integrity ==="
	./cvmvhd extract final.vhd final.img
	@if cmp -s original.img final.img; then echo "✅ PASS: Data preserved through all conversions"; else echo "❌ FAIL: Data corrupted during conversions" && exit 1; fi
	@echo "=== All expand/compact tests passed! ==="

test-fixed.vhd:
	./cvmvhd create test-fixed.vhd 1
	./cvmvhd dump sample.vhd| grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Expected disk_type=2 for fixed VHD" && exit 1)

ldd:
	ldd $(TARGET)
