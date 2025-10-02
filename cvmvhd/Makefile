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
	rm -f test-copy.vhd baseline-raw.img extracted-raw-fixed.img converted-dynamic.vhd
	rm -f dynamic-extracted.img reconverted-fixed.vhd final-without-footer.img

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
	$(MAKE) compact-expand-integrity-tests

compact-expand-integrity-tests:
	@if [ -f provided-fixed.vhd ]; then \
		echo "=== Found provided-fixed.vhd, running compact/expand integrity tests ==="; \
		$(MAKE) run-compact-expand-integrity-tests; \
	else \
		echo ""; \
		echo "⚠️  WARNING: provided-fixed.vhd not found"; \
		echo ""; \
		echo "=== Skipping compact/expand integrity tests ==="; \
		echo ""; \
		echo "To run compact/expand integrity tests:"; \
		echo "  1. Place your fixed VHD file as 'provided-fixed.vhd' in this directory"; \
		echo "  2. Run 'make compact-expand-integrity-tests' or 'make tests'"; \
		echo ""; \
	fi

run-compact-expand-integrity-tests:
	@echo "=== Step 0: Clean up any existing test files ==="
	rm -f test-copy.vhd baseline-raw.img extracted-raw-fixed.img converted-dynamic.vhd
	rm -f dynamic-extracted.img reconverted-fixed.vhd final-without-footer.img
	@echo "✅ PASS: Test environment cleaned"
	
	@echo "=== Step 1: Verify VHD format and validate data content ==="
	./cvmvhd dump provided-fixed.vhd | grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Input VHD is not a fixed VHD (expected disk_type=2)" && exit 1)
	@echo "✅ PASS: Input VHD is fixed format (disk_type=2)"
	@VHD_SIZE=$$(stat -c%s provided-fixed.vhd); \
	DATA_SIZE=$$(($$VHD_SIZE - 512)); \
	NON_ZERO_COUNT=$$(head -c $$DATA_SIZE provided-fixed.vhd | tr -d '\0' | wc -c); \
	if [ $$NON_ZERO_COUNT -eq 0 ]; then \
		echo "❌ FAIL: Provided VHD appears to be empty (no non-zero data found)"; \
		echo "Please provide a VHD that contains actual disk image data"; \
		exit 1; \
	else \
		echo "✅ PASS: VHD contains $$NON_ZERO_COUNT non-zero bytes of data"; \
	fi
	cp provided-fixed.vhd test-copy.vhd
	
	@echo "=== Step 2: Remove footer to create raw image ==="
	./cvmvhd remove test-copy.vhd
	@# Verify footer was removed by checking if dump fails (no VHD footer)
	@if ./cvmvhd dump test-copy.vhd 2>/dev/null; then echo "❌ FAIL: VHD footer still present after remove" && exit 1; else echo "✅ PASS: VHD footer removed successfully"; fi
	mv test-copy.vhd baseline-raw.img
	
	@echo "=== Step 3: Extract data from original VHD ==="
	./cvmvhd extract provided-fixed.vhd extracted-raw-fixed.img
	@echo "✅ PASS: Extracted raw image from original VHD"
	
	@echo "=== Step 4: Compare raw images (should be identical) ==="
	@if cmp -s baseline-raw.img extracted-raw-fixed.img; then echo "✅ PASS: Raw images match (remove and extract produce identical results)"; else echo "❌ FAIL: Raw images don't match" && exit 1; fi
	
	@echo "=== Step 5: Convert original VHD to dynamic format ==="
	./cvmvhd compact provided-fixed.vhd converted-dynamic.vhd
	./cvmvhd dump converted-dynamic.vhd | grep "disk_type=3" > /dev/null || (echo "❌ FAIL: Expected disk_type=3 for dynamic VHD" && exit 1)
	@echo "✅ PASS: Successfully converted to dynamic VHD (disk_type=3)"
	
	@echo "=== Step 6: Extract data from dynamic VHD ==="
	./cvmvhd extract converted-dynamic.vhd dynamic-extracted.img
	@echo "✅ PASS: Extracted raw image from dynamic VHD"
	
	@echo "=== Step 7: Verify dynamic extraction matches baseline ==="
	@if cmp -s baseline-raw.img dynamic-extracted.img; then echo "✅ PASS: Dynamic VHD extraction matches baseline"; else echo "❌ FAIL: Dynamic VHD data corrupted" && exit 1; fi
	
	@echo "=== Step 8: Convert dynamic VHD back to fixed format ==="
	./cvmvhd expand converted-dynamic.vhd reconverted-fixed.vhd
	./cvmvhd dump reconverted-fixed.vhd | grep "disk_type=2" > /dev/null || (echo "❌ FAIL: Expected disk_type=2 for fixed VHD" && exit 1)
	@echo "✅ PASS: Successfully converted back to fixed VHD (disk_type=2)"
	
	@echo "=== Step 9: Final integrity check - checksum data portion only ==="
	@BASELINE_CHECKSUM=$$(head -c -512 provided-fixed.vhd | sha256sum | cut -d' ' -f1); \
	FINAL_CHECKSUM=$$(head -c -512 reconverted-fixed.vhd | sha256sum | cut -d' ' -f1); \
	if [ "$$BASELINE_CHECKSUM" = "$$FINAL_CHECKSUM" ]; then \
		echo "✅ PASS: Round-trip data integrity preserved - checksums match"; \
	else \
		echo "❌ FAIL: Data corruption detected - checksums differ"; \
		echo "  Original: $$BASELINE_CHECKSUM"; \
		echo "  Final:    $$FINAL_CHECKSUM"; \
		exit 1; \
	fi
	
	@echo ""
	@echo "🎉 === ALL COMPACT/EXPAND INTEGRITY TESTS PASSED! ==="
	@echo "✅ Fixed → Raw (remove): OK"
	@echo "✅ Fixed → Raw (extract): OK" 
	@echo "✅ Fixed → Dynamic: OK"
	@echo "✅ Dynamic → Raw (extract): OK"
	@echo "✅ Dynamic → Fixed: OK"
	@echo "✅ Round-trip data integrity: OK"
	
	@echo ""
	@echo "=== Final cleanup ==="
	rm -f test-copy.vhd baseline-raw.img extracted-raw-fixed.img converted-dynamic.vhd
	rm -f dynamic-extracted.img reconverted-fixed.vhd final-without-footer.img
	@echo "✅ PASS: Test files cleaned up"

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
