/*
 * Demo TimerD0 in high-resolution regulated mode.
 * SMCLK = MCLK = 16MHz
 *
 * The reference input clock (SMCLK) is multiplied by a factor of 16 to generate a timer clock of 256MHz
 */
#include <msp430.h>
#include <stdint.h>
//#include <assert.h>

#define CAP_BUF_SIZE    32
static volatile uint16_t td0_overflow_cnt=0;
static volatile uint16_t td0_cap_ch0_index=0;
static volatile uint16_t td0_cap_ch0[3][CAP_BUF_SIZE];
static volatile uint16_t td0_cap_ch1_index=0;
static volatile uint16_t td0_cap_ch1[3][CAP_BUF_SIZE];

static void SetVcoreUp (unsigned int level);

int main(void) {
    volatile uint16_t overflow_val;
    volatile uint16_t timerd_val;

    WDTCTL = WDTPW | WDTHOLD;       // Stop watchdog timer

    // NOTE: Change core voltage one level at a time..
    SetVcoreUp (0x01);
    SetVcoreUp (0x02);
    SetVcoreUp (0x03);
    __delay_cycles(800000);

    // USC module configuration, Fdcoclockdiv = Fmclk = 16MHz
    UCSCTL8 &= ~SMCLKREQEN;         // disable SMCLK clock requests
    UCSCTL3 = (0*FLLREFDIV0)        // FLL ref divider 1
            + SELREF2;              // set REFOCLK as FLL reference clock source
    UCSCTL4 = SELA__REFOCLK         // ACLK = REFO
            + SELM__DCOCLKDIV       // MCLK = DCOCLKDIV
            + SELS__DCOCLKDIV;      // SMCLK = DCOCLKDIV
    __bis_SR_register(SCG0);        // disable FLL operation
    UCSCTL0	= 0x0000;               // lowest possible DCO, MOD
    UCSCTL1 = DISMOD_L              // modulation disabled
            + DCORSEL_6;            // DCO range for 16MHz operation
                                    // (see msp430f5172 datasheet, DCO freq. 10.7-39.0MHz)
    UCSCTL2 = FLLD_0                // D=FLLD=1, so that Fdco=16MHz
            + 487;                  // DCO multiplier for 16MHz
                                    // (N + 1) * FLLRef = Fdcodiv
                                    // (487 + 1) * 32768 = 16MHz (multiplier N = 487)
    __bic_SR_register(SCG0);        // re-enable FLL operation

    // worst-case settling time for the DCO when the DCO range bits have been
    // 32 x 32 x 16 MHz / 32,768 Hz = 500000 = MCLK cycles for DCO to settle
    __delay_cycles(500000);

    // Each timer_D (TD0, TD1) has 3 capture/compare channels with 2 capture signals
    // for each channel, i.e.timer_D0 can be used with following capture inputs
    // (selected with CCISx bits):
    // *P1.6   TD0.0   CCI0A   (J9 pin 15)         P1DIR.6=0   P1SEL.6=1   P1MAP.6=def
    // *P1.7   TD0.1   CCI1A   (J9 pin 16)         P1DIR.7=0   P1SEL.7=1   P1MAP.6=def

    // Continuous mode: TDxIFG i/r flag (prio. 57) set when timer counts from TDRmax to 0.

    // If a capture is performed:
    // - The TDCCRx reg is copied into the TDCLx reg
    // - The timer value is copied into the TDCCRx reg
    // - The capture overflow bit is set at a capture overflow condition
    // - The i/r flag CCIFG is set

    // CCI0A input on Schmartboard J9 pin 15 (P1.6)
    P1SEL |= BIT6;                  // function TD0.CCI0A
    P1DIR &= ~BIT6;                 // input
    //P1REN &= ~BIT6;                 // input with pullup/pulldown disabled
    P1REN |= BIT6;                  // input with pullup/pulldown enabled
    //P1OUT |= BIT6;                  // pullup
    P1OUT &= ~BIT6;                 // pulldown (to avoid spurious interrupts)

    // CCI1A input on Schmartboard J9 pin 16 (P1.7)
    P1SEL |= BIT7;                  // function TD0.CCI1A
    P1DIR &= ~BIT7;                 // input
    //P1REN &= ~BIT7;                 // input with pullup/pulldown disabled
    P1REN |= BIT7;                  // input with pullup/pulldown enabled
    //P1OUT |= BIT7;                  // pullup
    P1OUT &= ~BIT7;                 // pulldown (to avoid spurious interrupts)

    // Timer_D configuration
    TD0CTL0 = TDSSEL_2              // Use SMCLK
            + CNTL_0                // 16-bit timer
            + ID_0                  // Divide input clock by 1
            + MC_0                  // Halt timer until init is complete
            + TDIE;                 // TDIFG interrupt enabled
    TD0CTL1 |= TDCLKM_1;            // TD0 clock = Hi-res local clock
    TD0CTL2 = (0*TDCAPM0)           // single capture mode channel 0
            + (0*TDCAPM1);          // single capture mode channel 1
    TD0HCTL0 = TDHFW                // Fast wake-up mode
            + TDHD_0                // Set divider to 1
            + TDHM_1                // Multiply by 16
            + TDHREGEN              // Regulated mode
            + TDHEAEN               // Enhance accuracy
            + TDHEN;                // Enable Hi-Res
    TD0HCTL1 |= TDHCLKCR;           // Necessary for regulated mode with CLK> 15 MHz
    TD0CCTL0 = (0*0x1000)           // CCI0A
        //    + (3*0x1000)            // testing - VCC
        //    + 0xC000                // Capture on both raising and falling edges
            + 0x8000                // Capture on falling edge
        //    + 0x4000                // Capture on raising edge
            + OUTMOD_7              // Reset/Set mode
            + 0x0100                // capture mode
            + 0x0010;               // CCIFG interrupt enabled
    TD0CCTL1 = (0*0x1000)           // CCI1A
       //     + (3*0x1000)            // testing - VCC
       //     + 0xC000                // Capture on both raising and falling edges
            + 0x8000                // Capture on falling edge
       //     + 0x4000                // Capture on raising edge
            + OUTMOD_7              // Reset/Set mode
            + 0x0100                // capture mode
            + 0x0010;               // CCIFG interrupt enabled

    TD0CTL0 |= TDCLR + MC_2;        // Timer_D clear and continuos mode (counts up to TD0Rmax)
    __bis_SR_register(GIE);         // enable interrupts

#if 0
    overflow_val = overflow_cnt;
    timerd_val = get_timerd_val();

    if (overflow_val != overflow_cnt) {
        // error, an overflow just happened, try again
        overflow_val = overflow_cnt;
        timerd_val = get_timerd_val();
    }
#endif

    // LED test
    P1DIR |= 0x01;                  // P1.0 output direction
    for(;;) {
        P1OUT ^= 0x01;              // Toggle P1.0 using exclusive-OR
        __delay_cycles(2*1000000);
    //    TD0CCTL1 ^= 0x1000;         // toggle bit 12 of TD0CCTL1 (CCIS bit 0)
    }

    return 0;
}

// TDxCCR0 interrupt vector
#pragma vector=TIMER0_D0_VECTOR
__interrupt void timer_D0_channel0_hdlr(void)
{
    // The TDxCCR0 CCIFG flag is automatically reset then the TDxCCR0 interrupt
    // request is serviced

    // CCIFG0 interrupt - capture event
    // read input level through CCI bit
    td0_cap_ch0[0][td0_cap_ch0_index] = TD0CCTL0 & CCI;

    // overflow counter
    td0_cap_ch0[1][td0_cap_ch0_index] = td0_overflow_cnt;

    // TDxCCR0 contains the timer value from the latest capture
    td0_cap_ch0[2][td0_cap_ch0_index] = TD0CCR0;

    if(++td0_cap_ch0_index == CAP_BUF_SIZE)
        td0_cap_ch0_index = 0;

    // make sure that COV=0, otherwise report error and clear COV
    //TD0CCTL0 & COV
}

// TDxIV interrupt vector
#pragma vector=TIMER0_D1_VECTOR
__interrupt void timer_D0_channel1_hdlr(void)
{
    // read access of the TD0IV register automatically resets the highest-pending
    // interrupt flag
    volatile uint16_t tdiv = TD0IV;

    if (tdiv == TD0IV_TD0IFG) {
        // TD0IFG interrupt - counter overflow
        td0_overflow_cnt++;
    }
    else { // tdiv == TD0IV_TDCCR1
        // TD0CCR1 CCIFG interrupt - capture event
        // read input level through CCI bit
        td0_cap_ch1[0][td0_cap_ch1_index] = TD0CCTL1 & CCI;

        // overflow counter
        td0_cap_ch1[1][td0_cap_ch1_index] = td0_overflow_cnt;

        // TDxCCR1 contains the timer value from the latest capture
        td0_cap_ch1[2][td0_cap_ch1_index] = TD0CCR1;

        if(++td0_cap_ch1_index == CAP_BUF_SIZE)
            td0_cap_ch1_index = 0;

        // make sure that COV=0, otherwise report error and clear COV
        //TD0CCTL1 & COV
    }
}

static void SetVcoreUp (unsigned int level)
{
    //
    // Change VCORE voltage level
    //
    PMMCTL0_H = PMMPW_H;                    // Open PMM registers for write
    SVSMHCTL = SVSHE                        // Set SVS/SVM high side new level
           + SVSHRVL0 * level
           + SVMHE
           + SVSMHRRL0 * level;
    SVSMLCTL = SVSLE                        // Set SVM low side to new level
           + SVMLE
           + SVSMLRRL0 * level;
    while ((PMMIFG & SVSMLDLYIFG) == 0);    // Wait till SVM is settled
    PMMIFG &= ~(SVMLVLRIFG + SVMLIFG);      // Clear already set flags
    PMMCTL0_L = PMMCOREV0 * level;          // Set VCore to new level
    if ((PMMIFG & SVMLIFG))                 // Wait till new level reached
    while ((PMMIFG & SVMLVLRIFG) == 0);
    SVSMLCTL = SVSLE                        // Set SVS/SVM low side to new level
          + SVSLRVL0 * level
          + SVMLE
          + SVSMLRRL0 * level;
    PMMCTL0_H = 0x00;                       // Lock PMM registers for write access
}

