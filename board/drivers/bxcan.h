// IRQs: CAN1_TX, CAN1_RX0, CAN1_SCE
//       CAN2_TX, CAN2_RX0, CAN2_SCE
//       CAN3_TX, CAN3_RX0, CAN3_SCE

CAN_TypeDef *cans[] = {CAN1, CAN2, CAN3};

bool can_set_speed(uint8_t can_number) {
  bool ret = true;
  CAN_TypeDef *CAN = CANIF_FROM_CAN_NUM(can_number);
  uint8_t bus_number = BUS_NUM_FROM_CAN_NUM(can_number);

  ret &= llcan_set_speed(CAN, bus_config[bus_number].can_speed, can_loopback, (unsigned int)(can_silent) & (1U << can_number));
  return ret;
}

// TODO: Cleanup with new abstraction
void can_set_gmlan(uint8_t bus) {
  if(current_board->has_hw_gmlan){
    // first, disable GMLAN on prev bus
    uint8_t prev_bus = bus_config[3].can_num_lookup;
    if (bus != prev_bus) {
      switch (prev_bus) {
        case 1:
        case 2:
          puts("Disable GMLAN on CAN");
          puth(prev_bus + 1U);
          puts("\n");
          current_board->set_can_mode(CAN_MODE_NORMAL);
          bus_config[prev_bus].bus_lookup = prev_bus;
          bus_config[prev_bus].can_num_lookup = prev_bus;
          bus_config[3].can_num_lookup = -1;
          bool ret = can_init(prev_bus);
          UNUSED(ret);
          break;
        default:
          // GMLAN was not set on either BUS 1 or 2
          break;
      }
    }

    // now enable GMLAN on the new bus
    switch (bus) {
      case 1:
      case 2:
        puts("Enable GMLAN on CAN");
        puth(bus + 1U);
        puts("\n");
        current_board->set_can_mode((bus == 1U) ? CAN_MODE_GMLAN_CAN2 : CAN_MODE_GMLAN_CAN3);
        bus_config[bus].bus_lookup = 3;
        bus_config[bus].can_num_lookup = -1;
        bus_config[3].can_num_lookup = bus;
        bool ret = can_init(bus);
        UNUSED(ret);
        break;
      case 0xFF:  //-1 unsigned
        break;
      default:
        puts("GMLAN can only be set on CAN2 or CAN3\n");
        break;
    }
  } else {
    puts("GMLAN not available on black panda\n");
  }
}

// CAN error
void can_sce(CAN_TypeDef *CAN) {
  ENTER_CRITICAL();

  #ifdef DEBUG
    if (CAN==CAN1) puts("CAN1:  ");
    if (CAN==CAN2) puts("CAN2:  ");
    #ifdef CAN3
      if (CAN==CAN3) puts("CAN3:  ");
    #endif
    puts("MSR:");
    puth(CAN->MSR);
    puts(" TSR:");
    puth(CAN->TSR);
    puts(" RF0R:");
    puth(CAN->RF0R);
    puts(" RF1R:");
    puth(CAN->RF1R);
    puts(" ESR:");
    puth(CAN->ESR);
    puts("\n");
  #endif

  can_err_cnt += 1;
  llcan_clear_send(CAN);
  EXIT_CRITICAL();
}

// ***************************** CAN *****************************
void process_can(uint8_t can_number) {
  if (can_number != 0xffU) {

    ENTER_CRITICAL();

    CAN_TypeDef *CAN = CANIF_FROM_CAN_NUM(can_number);
    uint8_t bus_number = BUS_NUM_FROM_CAN_NUM(can_number);

    // check for empty mailbox
    CAN_FIFOMailBox_TypeDef to_send;
    if ((CAN->TSR & CAN_TSR_TME0) == CAN_TSR_TME0) {
      // add successfully transmitted message to my fifo
      if ((CAN->TSR & CAN_TSR_RQCP0) == CAN_TSR_RQCP0) {
        can_txd_cnt += 1;

        if ((CAN->TSR & CAN_TSR_TXOK0) == CAN_TSR_TXOK0) {
          CAN_FIFOMailBox_TypeDef to_push;
          to_push.RIR = CAN->sTxMailBox[0].TIR;
          to_push.RDTR = (CAN->sTxMailBox[0].TDTR & 0xFFFF000FU) | ((CAN_BUS_RET_FLAG | bus_number) << 4);
          to_push.RDLR = CAN->sTxMailBox[0].TDLR;
          to_push.RDHR = CAN->sTxMailBox[0].TDHR;
          can_send_errs += can_push(&can_rx_q, &to_push) ? 0U : 1U;
        }

        if ((CAN->TSR & CAN_TSR_TERR0) == CAN_TSR_TERR0) {
          #ifdef DEBUG
            puts("CAN TX ERROR!\n");
          #endif
        }

        if ((CAN->TSR & CAN_TSR_ALST0) == CAN_TSR_ALST0) {
          #ifdef DEBUG
            puts("CAN TX ARBITRATION LOST!\n");
          #endif
        }

        // clear interrupt
        // careful, this can also be cleared by requesting a transmission
        CAN->TSR |= CAN_TSR_RQCP0;
      }

      if (can_pop(can_queues[bus_number], &to_send)) {
        can_tx_cnt += 1;
        // only send if we have received a packet
        CAN->sTxMailBox[0].TDLR = to_send.RDLR;
        CAN->sTxMailBox[0].TDHR = to_send.RDHR;
        CAN->sTxMailBox[0].TDTR = to_send.RDTR;
        CAN->sTxMailBox[0].TIR = to_send.RIR;

        usb_cb_ep3_out_complete();
      }
    }

    EXIT_CRITICAL();
  }
}

// CAN receive handlers
// blink blue when we are receiving CAN messages
void can_rx(uint8_t can_number) {
  CAN_TypeDef *CAN = CANIF_FROM_CAN_NUM(can_number);
  uint8_t bus_number = BUS_NUM_FROM_CAN_NUM(can_number);
  while ((CAN->RF0R & CAN_RF0R_FMP0) != 0) {
    can_rx_cnt += 1;

    // can is live
    pending_can_live = 1;

    // add to my fifo
    CAN_FIFOMailBox_TypeDef to_push;
    to_push.RIR = CAN->sFIFOMailBox[0].RIR;
    to_push.RDTR = CAN->sFIFOMailBox[0].RDTR;
    to_push.RDLR = CAN->sFIFOMailBox[0].RDLR;
    to_push.RDHR = CAN->sFIFOMailBox[0].RDHR;

    // modify RDTR for our API
    to_push.RDTR = (to_push.RDTR & 0xFFFF000F) | (bus_number << 4);

    // forwarding (panda only)
    int bus_fwd_num = safety_fwd_hook(bus_number, &to_push);
    if (bus_fwd_num != -1) {
      CAN_FIFOMailBox_TypeDef to_send;
      to_send.RIR = to_push.RIR | 1; // TXRQ
      to_send.RDTR = to_push.RDTR;
      to_send.RDLR = to_push.RDLR;
      to_send.RDHR = to_push.RDHR;
      can_send(&to_send, bus_fwd_num, true);
    }

    can_rx_errs += safety_rx_hook(&to_push) ? 0U : 1U;
    ignition_can_hook(&to_push);

    current_board->set_led(LED_BLUE, true);
    can_send_errs += can_push(&can_rx_q, &to_push) ? 0U : 1U;

    // next
    CAN->RF0R |= CAN_RF0R_RFOM0;
  }
}

void CAN1_TX_IRQ_Handler(void) { process_can(0); }
void CAN1_RX0_IRQ_Handler(void) { can_rx(0); }
void CAN1_SCE_IRQ_Handler(void) { can_sce(CAN1); }

void CAN2_TX_IRQ_Handler(void) { process_can(1); }
void CAN2_RX0_IRQ_Handler(void) { can_rx(1); }
void CAN2_SCE_IRQ_Handler(void) { can_sce(CAN2); }

void CAN3_TX_IRQ_Handler(void) { process_can(2); }
void CAN3_RX0_IRQ_Handler(void) { can_rx(2); }
void CAN3_SCE_IRQ_Handler(void) { can_sce(CAN3); }

bool can_init(uint8_t can_number) {
  bool ret = false;

  REGISTER_INTERRUPT(CAN1_TX_IRQn, CAN1_TX_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_1)
  REGISTER_INTERRUPT(CAN1_RX0_IRQn, CAN1_RX0_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_1)
  REGISTER_INTERRUPT(CAN1_SCE_IRQn, CAN1_SCE_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_1)
  REGISTER_INTERRUPT(CAN2_TX_IRQn, CAN2_TX_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_2)
  REGISTER_INTERRUPT(CAN2_RX0_IRQn, CAN2_RX0_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_2)
  REGISTER_INTERRUPT(CAN2_SCE_IRQn, CAN2_SCE_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_2)
  REGISTER_INTERRUPT(CAN3_TX_IRQn, CAN3_TX_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_3)
  REGISTER_INTERRUPT(CAN3_RX0_IRQn, CAN3_RX0_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_3)
  REGISTER_INTERRUPT(CAN3_SCE_IRQn, CAN3_SCE_IRQ_Handler, CAN_INTERRUPT_RATE, FAULT_INTERRUPT_RATE_CAN_3)

  if (can_number != 0xffU) {
    CAN_TypeDef *CAN = CANIF_FROM_CAN_NUM(can_number);
    ret &= can_set_speed(can_number);
    ret &= llcan_init(CAN);
    // in case there are queued up messages
    process_can(can_number);
  }
  return ret;
}
