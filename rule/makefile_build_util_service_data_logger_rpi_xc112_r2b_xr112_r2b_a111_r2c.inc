BUILD_ALL += out/util_service_data_logger_rpi_xc112_r2b_xr112_r2b_a111_r2c

out/util_service_data_logger_rpi_xc112_r2b_xr112_r2b_a111_r2c : \
					out/util_service_data_logger.o \
					libacconeer.a \
					libacconeer_a111_r2c.a \
					out/libcustomer.a \
					libacc_service.a \
					out/acc_board_rpi_xc112_r2b_xr112_r2b.o
	@echo "    Linking $(notdir $@)"
	@mkdir -p out
	@$(LINK.o) -Wl,--start-group $^ -Wl,--end-group $(LOADLIBES) $(LDLIBS) -o $@
