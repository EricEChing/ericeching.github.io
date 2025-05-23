/*
 * Copyright (c) 2023, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ti_msp_dl_config.h"
#include <ti/devices/msp/msp.h>

typdef enum state {
    OFF,
    CALIBRATING,
    READING
} state;

#define WIDTH 128
#define HEIGHT 64



#define I2C_TX_MAX_PACKET_SIZE (16)
#define I2C_TX_PACKET_SIZE (16)
#define I2C_RX_MAX_PACKET_SIZE (16)
#define I2C_RX_PACKET_SIZE (16)
#define I2C_TARGET_ADDRESS (0x11)
uint8_t gTxPacket[I2C_TX_MAX_PACKET_SIZE] = {0x00, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
uint32_t gTxLen, gTxCount;
uint8_t gRxPacket[I2C_RX_MAX_PACKET_SIZE];
uint32_t gRxLen, gRxCount;
enum I2cControllerStatus {
    I2C_STATUS_IDLE = 0,
    I2C_STATUS_TX_STARTED,
    I2C_STATUS_TX_INPROGRESS,
    I2C_STATUS_TX_COMPLETE,
    I2C_STATUS_RX_STARTED,
    I2C_STATUS_RX_INPROGRESS,
    I2C_STATUS_RX_COMPLETE,
    I2C_STATUS_ERROR,
} gI2cControllerStatus;


int main(void)
{
    SYSCFG_DL_init();

    NVIC_EnableIRQ(I2C_INST_INT_IRQN);

    struct state current_state = OFF;


    while (1) {
        switch(current_state) {
        case OFF:
            if(!GPIOA->PA28) {
                current_state = CALIBRATING;
            } else {
                current_state = OFF;
            }
        case CALIBRATING:
            calibrate();
            current_state = READING;
        case READING:
            read_routine();
            if(!GPIOA->PA28) {
                current_state = OFF;
            } else if(!GPIOA->PA19) {
                current_state = CALIBRATING;
            } else {
                current_state = READING;
            }
        }

    }

}


void read_routine(void) {
    gTxPacket = {0x03, 0, 0};
    write("sensor");
    read();
    display();
}


void calibrate(void) {
    gTxPacket = {0b0101100, 0x26, 0x00};
    write("sensor");
}

void display(void) {

    // 128 x 64

    if ((!buffer) && !(buffer = (uint8_t *)malloc(WIDTH * ((HEIGHT + 7) / 8))))
      exit(1);

    uint8_t *pBuf = &buffer[(y / 8) * WIDTH + x], mask = 1 << (y & 7);

    for (size_t i = 0; i < 8*I2C_RX_MAX_PACKET_SIZE; i++) {
            for (int bit = 7; bit >= 0; bit--) {
                uint8_t bit_value = (gRxPacket[i] >> bit) & 1;
                if(bit==1) {
                    while (w--) {
                        *pBuf++ |= mask;
                    };
                } else {
                    mask = ~mask;
                    while (w--) {
                      *pBuf++ &= mask;
                    };
                }
            }
        }


    uint16_t count = WIDTH * ((HEIGHT + 7) / 8);
    uint8_t *ptr = buffer;
    uint8_t index = 1;
    uint8_t packetSize = 1;

    while(count--) {
        if(packetSize == I2C_TX_MAX_PACKET_SIZE) {
            write("display");
            gTxPacket = {0x0, 0x0, 0x0};
            index = 1;
            packetSize = 1;
        }
        if(gTxPacket[0] != 0x40) {
            gTxPacket[0] = 0x40;
        }
        gTxPacket[index] = *ptr++;
        index++;
        packetSize++;
    }
    write("display");

}



void write(char []target) {

    if(target=="display") {
        I2C_TARGET_ADDRESS = b0111100;
    } else {
        I2C_TARGET_ADDRESS = b0101100;
    }

    gI2cControllerStatus = I2C_STATUS_IDLE;

    gTxLen = I2C_TX_PACKET_SIZE;
    /*
     * Fill the FIFO
     *  The FIFO is 8-bytes deep, and this function will return number
     *  of bytes written to FIFO */
    gTxCount = DL_I2C_fillControllerTXFIFO(I2C_INST, &gTxPacket[0], gTxLen);
    /* Enable TXFIFO trigger interrupt if there are more bytes to send */
    if (gTxCount < gTxLen) {
        DL_I2C_enableInterrupt(
            I2C_INST, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
    } else {
        DL_I2C_disableInterrupt(
            I2C_INST, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
    }

    /*
     * Send the packet to the controller.
     * This function will send Start + Stop automatically.
     */
    gI2cControllerStatus = I2C_STATUS_TX_STARTED;
    while (!(
        DL_I2C_getControllerStatus(I2C_INST) & DL_I2C_CONTROLLER_STATUS_IDLE))
        ;
    DL_I2C_startControllerTransfer(
        I2C_INST, I2C_TARGET_ADDRESS, DL_I2C_CONTROLLER_DIRECTION_TX, gTxLen);

    /* Wait until the Controller sends all bytes */
    while ((gI2cControllerStatus != I2C_STATUS_TX_COMPLETE) &&
           (gI2cControllerStatus != I2C_STATUS_ERROR)) {
        __WFE();
    }

    while (DL_I2C_getControllerStatus(I2C_INST) &
           DL_I2C_CONTROLLER_STATUS_BUSY_BUS)
        ;

    /* Trap if there was an error */
    if (DL_I2C_getControllerStatus(I2C_INST) &
        DL_I2C_CONTROLLER_STATUS_ERROR) {
        /* LED will remain high if there is an error */
        __BKPT(0);
    }

    while (!(
        DL_I2C_getControllerStatus(I2C_INST) & DL_I2C_CONTROLLER_STATUS_IDLE))
        ;

}

void read() {
    I2C_TARGET_ADDRESS = b0111100;
    /* Send a read request to Target */
    gRxLen               = I2C_RX_PACKET_SIZE;
    gRxCount             = 0;
    gI2cControllerStatus = I2C_STATUS_RX_STARTED;
    DL_I2C_startControllerTransfer(
        I2C_INST, I2C_TARGET_ADDRESS, DL_I2C_CONTROLLER_DIRECTION_RX, gRxLen);

    /* Wait for all bytes to be received in interrupt */
    while (gI2cControllerStatus != I2C_STATUS_RX_COMPLETE) {
        __WFE();
    }

    while (DL_I2C_getControllerStatus(I2C_INST) &
           DL_I2C_CONTROLLER_STATUS_BUSY_BUS)
        ;
}


void I2C_INST_IRQHandler(void)
{
    switch (DL_I2C_getPendingInterrupt(I2C_INST)) {
        case DL_I2C_IIDX_CONTROLLER_RX_DONE:
            gI2cControllerStatus = I2C_STATUS_RX_COMPLETE;
            break;
        case DL_I2C_IIDX_CONTROLLER_TX_DONE:
            DL_I2C_disableInterrupt(
                I2C_INST, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
            gI2cControllerStatus = I2C_STATUS_TX_COMPLETE;
            break;
        case DL_I2C_IIDX_CONTROLLER_RXFIFO_TRIGGER:
            gI2cControllerStatus = I2C_STATUS_RX_INPROGRESS;
            /* Receive all bytes from target */
            while (DL_I2C_isControllerRXFIFOEmpty(I2C_INST) != true) {
                if (gRxCount < gRxLen) {
                    gRxPacket[gRxCount++] =
                        DL_I2C_receiveControllerData(I2C_INST);
                } else {
                    /* Ignore and remove from FIFO if the buffer is full */
                    DL_I2C_receiveControllerData(I2C_INST);
                }
            }
            break;
        case DL_I2C_IIDX_CONTROLLER_TXFIFO_TRIGGER:
            gI2cControllerStatus = I2C_STATUS_TX_INPROGRESS;
            /* Fill TX FIFO with next bytes to send */
            if (gTxCount < gTxLen) {
                gTxCount += DL_I2C_fillControllerTXFIFO(
                    I2C_INST, &gTxPacket[gTxCount], gTxLen - gTxCount);
            }
            break;
            /* Not used for this example */
        case DL_I2C_IIDX_CONTROLLER_ARBITRATION_LOST:
        case DL_I2C_IIDX_CONTROLLER_NACK:
            if ((gI2cControllerStatus == I2C_STATUS_RX_STARTED) ||
                (gI2cControllerStatus == I2C_STATUS_TX_STARTED)) {
                /* NACK interrupt if I2C Target is disconnected */
                gI2cControllerStatus = I2C_STATUS_ERROR;
            }
        case DL_I2C_IIDX_CONTROLLER_RXFIFO_FULL:
        case DL_I2C_IIDX_CONTROLLER_TXFIFO_EMPTY:
        case DL_I2C_IIDX_CONTROLLER_START:
        case DL_I2C_IIDX_CONTROLLER_STOP:
        case DL_I2C_IIDX_CONTROLLER_EVENT1_DMA_DONE:
        case DL_I2C_IIDX_CONTROLLER_EVENT2_DMA_DONE:
        default:
            break;
    }
}
