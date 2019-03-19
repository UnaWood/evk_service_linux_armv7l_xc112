BUILD_ALL += out/example_service_power_bins_rpi_xc112_r2b_xr112_r2b_a111_r2c

out/example_service_power_bins_rpi_xc112_r2b_xr112_r2b_a111_r2c : \
					out/example_service_power_bins.o \
					libacconeer.a \
					libacconeer_a111_r2c.a \
					out/libcustomer.a \
					libacc_service.a \
					out/acc_board_rpi_xc112_r2b_xr112_r2b.o
	@echo "    Linking $(notdir $@)"
	@mkdir -p out
	@$(LINK.o) -Wl,--start-group $^ -Wl,--end-group $(LOADLIBES) $(LDLIBS) -o $@
