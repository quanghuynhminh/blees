#include <stdio.h>
#include <stdint.h>

#include "app_error.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "spi_driver.h"

#include "adxl362.h"

//Register Address defines
#define DEVID_AD 0x00
#define DEVID_MST 0x01
#define PARTID 0x02
#define REVID 0x03
#define XDATA 0x08
#define YDATA 0x09
#define ZDATA 0x0A
#define STATUS 0x0B
#define FIFO_ENTRIES_L 0x0C
#define FIFO_ENTRIES_H 0x0D
#define XDATA_L 0x0E
#define XDATA_H 0x0F
#define YDATA_L 0x10
#define YDATA_H 0x11
#define ZDATA_L 0x12
#define ZDATA_H 0x13
#define TEMP_L 0x14
#define TEMP_H 0x15
#define SOFT_RESET 0x1F
#define RESET_CODE 0x52 // for soft_reset reg
#define THRESH_ACT_L 0x20
#define THRESH_ACT_H 0x21
#define TIME_ACT 0x22
#define THRESH_INACT_L 0x23
#define THRESH_INACT_H 0x24
#define TIME_INACT_L 0x25
#define TIME_INACT_H 0x26
#define ACT_INACT_CTL 0x27
#define FIFO_CTL 0x28
#define FIFO_SAMPLES 0x29
#define INTMAP1 0x2A
#define INTMAP2 0x2B
#define FILTER_CTL 0x2C
#define POWER_CTL 0x2D
#define SELF_TEST 0x2E
#define READ_FIFO 0x0D

//Act_Inact_Ctl defines
#define DEFAULT_MODE 0x00
#define LINKED_MODE 0x10
#define LOOP_MODE 0x30
#define INACT_EN 0x04
#define ACT_EN 0x01
#define INACT_REF_EN 0x08
#define ACT_REF_EN 0x02

//Fifo_Ctl defines
#define STORE_TEMP_MODE 0x04
#define GREATER_THAN_255 0x08

//Power_Ctl defines
#define STANDBY_MODE 0x00
#define MEASUREMENT_MODE 0x02
#define AUTOSLEEP_MODE_EN 0x03
#define WAKEUP_MODE_EN 0x08

void adxl362_config_interrupt_mode(adxl362_interrupt_mode i_mode, bool use_referenced_activity, bool use_referenced_inactivity){

	uint8_t data[1] = {0x00};

	if (i_mode == adxl362_INTERRUPT_LINKED){
		data[0] = LINKED_MODE;
	}
	else if (i_mode == adxl362_INTERRUPT_LOOP){
		data[0] = LOOP_MODE;
	}
	//else default;

	if (use_referenced_inactivity){
		data[0] = data[0] | INACT_REF_EN; 
	}
	if (use_referenced_activity){
		data[0] = data[0] | ACT_REF_EN;
	}

	spi_write_reg(ACT_INACT_CTL, data, 1);

}

//if intmap_1 = true, config for intpin 1
// if intmap_1 = false, config for intpin 2
void adxl362_config_INTMAP(adxl362_interrupt_map_t * int_map, bool intmap_1){

	uint8_t data[1] = {0x00};

	if (int_map->DATA_READY){
		data[0] = 0x01;
	} 
	if (int_map->FIFO_READY){
		data[0] = data[0] | 0x02;
	}
	if (int_map->FIFO_WATERMARK){
		data[0] = data[0] | 0x04;
	}
	if (int_map->FIFO_OVERRUN){
		data[0] = data[0] | 0x08;
	}
	if (int_map->ACT){
		data[0] = data[0] | 0x10;
	}
	if (int_map->INACT){
		data[0] = data[0] | 0x20;
	}
	if (int_map->AWAKE){
		data[0] = data[0] | 0x40;
	}
	if (int_map->INT_LOW){
		data[0] = data[0] | 0x80;
	}


	if (intmap_1){
		spi_write_reg(INTMAP1, data, 1);
	}
	else {
		spi_write_reg(INTMAP2, data, 1);
	}

}


//only 11 bits are used

void adxl362_set_activity_threshold(uint16_t act_threshold){

	uint8_t data[1] = { 0x00FF & act_threshold};
	spi_write_reg(THRESH_ACT_L, data, 1);

	spi_read_reg(THRESH_ACT_H, data, 1);
	data[0] = data[0] & 0xF8;
	data[0] = (data[0] | ((act_threshold & 0x0700) >> 8) );

	spi_write_reg(THRESH_ACT_H, data, 1);

}

void adxl362_set_inactivity_threshold(uint16_t inact_threshold){

	uint8_t data[1] = { 0x00FF & inact_threshold};
	spi_write_reg(THRESH_INACT_L, data, 1);
	
	data[0] = (uint8_t)((0x0700 & inact_threshold) >> 8);
	spi_write_reg(THRESH_INACT_H, data, 1);

}


//ignored if device is on wake-up mode
void adxl362_set_inactivity_time(uint16_t inact_time){

	uint8_t data[2] = {0x00, 0x00};
	data[0] = 0x00FF & inact_time;
	data[1] = (0xFF00 & inact_time) >> 8;

	spi_write_reg(TIME_INACT_L, data, 1);
	spi_write_reg(TIME_INACT_H, data + 1, 1);


}

void adxl362_set_activity_time(uint8_t act_time){

	uint8_t data[1] = {act_time};
	spi_write_reg(TIME_ACT, data, 1);

}

void adxl362_activity_interrupt_enable(){

	uint8_t data[1] = {0x00};

	spi_read_reg(ACT_INACT_CTL, data, 1);

	data[0] = data[0] | ACT_EN;

	spi_write_reg(ACT_INACT_CTL, data, 1);

	//clear activity interrupt
	spi_read_reg(STATUS, data, 1);


}

void adxl362_inactivity_interrupt_enable(){

	uint8_t data[1] = {0x00};

	spi_read_reg(ACT_INACT_CTL, data, 1);

	data[0] = data[0] | INACT_EN;

	spi_write_reg(ACT_INACT_CTL, data, 1);

	//clear inactivity interrupt
	spi_read_reg(STATUS, data, 1);

}

void adxl362_activity_inactivity_interrupt_enable(){

	uint8_t data[1] = {0x00};

	spi_read_reg(ACT_INACT_CTL, data, 1);

	data[0] = data[0] | INACT_EN | ACT_EN;

	spi_write_reg(ACT_INACT_CTL, data, 1);

	//clear activity/inactivity interrupt
	spi_read_reg(STATUS, data, 1);

}

void adxl362_num_FIFO_samples_ready(uint16_t *num_ready){

	uint8_t n_ready[2] = {0x00, 0x00};

	spi_read_reg(FIFO_ENTRIES_L, n_ready, 1);
	spi_read_reg(FIFO_ENTRIES_H, n_ready + 1, 1);

	*num_ready = (uint16_t)(n_ready[0] | ( (0x03 & n_ready[1]) << 8));


}

void adxl362_read_FIFO(uint8_t * buf, uint16_t num_samples){

	uint8_t command[1] = {READ_FIFO};

	nrf_gpio_pin_clear(SPI_SS_PIN);
    spi_write(READ_FIFO);

    int i = 0;
    do{
        spi_read(buf + i);
        i++;
    } while (i < num_samples);

    nrf_gpio_pin_set(SPI_SS_PIN);
}

void adxl362_parse_FIFO(uint8_t * buf_in, int16_t * buf_out, uint16_t num_samples){


	for(int i = 0, j = 0; i < num_samples; i = i+2, j++){

		if ((buf_in[i+1] & 0x20) == 0x20){
			buf_out[j] = (int16_t)( 0xC0 | (buf_in[i] | ((0x3F & buf_in[i+1]) << 8) ));

		}
		else{
			buf_out[j] = (int16_t)(buf_in[i] | ((0x3F & buf_in[i+1]) << 8) );
		}

	}

}

void adxl362_config_FIFO(adxl362_fifo_mode f_mode, bool store_temp, uint16_t num_samples){

	uint8_t data[1] = { num_samples & 0x00FF};

	uint8_t read_data[1] = {0x00};

	spi_write_reg(FIFO_SAMPLES, data, 1);

	spi_read_reg(FIFO_SAMPLES, read_data, 1);

	data[0] = f_mode;

	if (store_temp){

		data[0] = data[0] | STORE_TEMP_MODE;

	}

	if ( num_samples > 255){

		data[0] = data[0] | GREATER_THAN_255; //AH bit set
	}

	spi_write_reg(FIFO_CTL, data, 1);

	spi_read_reg(FIFO_CTL, read_data, 1);

}

/**********SAMPLE 8 MSB OF DATA***********/

void adxl362_sample_accel_byte_x(uint8_t * x_data){

	spi_read_reg(XDATA, x_data, 1);

}

void adxl362_sample_accel_byte_y(uint8_t * y_data){

	spi_read_reg(YDATA, y_data, 1);

}

void adxl362_sample_accel_byte_z(uint8_t * z_data){

	spi_read_reg(ZDATA, z_data, 1);

}


/**********SAMPLE 16 BITS OF DATA***********/

void adxl362_sample_accel_word_x(uint8_t * x_data){

	spi_read_reg(XDATA_L, x_data, 1);

	spi_read_reg(XDATA_H, x_data + 1, 1);

}

void adxl362_sample_accel_word_y(uint8_t * y_data){

	spi_read_reg(YDATA_L, y_data, 1);

	spi_read_reg(YDATA_H, y_data + 1, 1);

}

void adxl362_sample_accel_word_z(uint8_t * z_data){

	spi_read_reg(ZDATA_L, z_data, 1);

	spi_read_reg(ZDATA_H, z_data + 1, 1);

}


uint8_t adxl362_read_status_reg(){

	uint8_t status_reg_data[1] = {0};

	spi_read_reg(STATUS, status_reg_data, 1);

	return status_reg_data[0];

}

void adxl362_sample_accel_word(uint8_t * x_data, uint8_t * y_data, uint8_t * z_data){

	adxl362_sample_accel_word_x(x_data);

	adxl362_sample_accel_word_y(y_data);

	adxl362_sample_accel_word_z(z_data);

}


void adxl362_sample_accel_byte(uint8_t * x_data, uint8_t * y_data, uint8_t * z_data){

	adxl362_sample_accel_byte_x(x_data);

	adxl362_sample_accel_byte_y(y_data);

	adxl362_sample_accel_byte_z(z_data);

}

//if measure = 0 standby mode, if measure = 1, measurement mode
void adxl362_accelerometer_init(adxl362_noise_mode n_mode, bool measure, bool autosleep_en, bool wakeup_en){

	
	spi_init();

    // send a soft reset to the accelerometer
    uint8_t data[1] = {RESET_CODE};
    spi_write_reg(SOFT_RESET, data, 1);

    //wait for device to be reset
    for (int i = 0; i < 100; i++);
    
    if (measure){
    	data[0] = MEASUREMENT_MODE;
    }
    if (autosleep_en){
    	data[0] = data[0] | AUTOSLEEP_MODE_EN;
    }
    if (wakeup_en){
    	data[0] = data[0] | WAKEUP_MODE_EN;
    }

    data[0] = data[0] | (n_mode << 4);

    spi_write_reg(POWER_CTL, data, 1);

}


void adxl362_config_measurement_range(adxl362_measurement_range m_range){

	uint8_t data[1] = {0x00};

	spi_read_reg(FILTER_CTL, data, 1);	

	data[0] = data[0] & 0x3F;

	data[0] =  data[0] | (m_range << 6);

	spi_write_reg(FILTER_CTL, data, 1);


}


void adxl362_read_dev_id(uint8_t *buf){

	spi_read_reg(PARTID, buf, 1);


}