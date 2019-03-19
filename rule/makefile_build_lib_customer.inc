BUILD_LIBS += out/libcustomer.a

out/libcustomer.a : $(addprefix out/,$(notdir $(patsubst %.c,%.o,$(wildcard source/acc_driver_*.c)))) \
		    $(addprefix out/,$(notdir $(patsubst %.c,%.o,$(wildcard source/acc_os*.c))))
	@echo "    Creating archive $(notdir $@)"
	@rm -f $@
	@$(AR) cr $@ $^
