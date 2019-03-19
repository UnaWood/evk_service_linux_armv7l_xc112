BUILD_ALL += out/example_detector_distance_peak_rpi_xc112_r2b_xr112_r2b_a111_r2c

out/example_detector_distance_peak_rpi_xc112_r2b_xr112_r2b_a111_r2c : \
					out/example_detector_distance_peak.o \
					libacconeer.a \
					libacconeer_a111_r2c.a \
					out/libcustomer.a \
					libacc_service.a \
					libacc_detector_distance_peak.a \
					out/acc_board_rpi_xc112_r2b_xr112_r2b.o
	@echo "    Linking $(notdir $@)"
	@mkdir -p out
	@$(LINK.o) -Wl,--start-group $^ -Wl,--end-group $(LOADLIBES) $(LDLIBS) -o $@
