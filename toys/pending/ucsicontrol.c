/* ucsicontrol.c - control/query USB Type-C Connector System Software Interface
 *
 * Copyright 2026 Madhu M <madhu.m@intel.com>
 *
 * Talks directly to the kernel UCSI debugfs interface exposed under
 * /sys/kernel/debug/usb/ucsi/.../{command,response}.

USE_UCSICONTROL(NEWTOY(ucsicontrol, 0, TOYFLAG_USR|TOYFLAG_SBIN|TOYFLAG_NEEDROOT))

config UCSICONTROL
  bool "ucsicontrol"
  default n
  help
    Usage: ucsicontrol [OPTIONS]
    Note: ucsicontrol must be run as root.
    Options:
      --help, -h                            Show this help message
      --conn_rst <conn_num> <soft/hard>     Reset the Connector
      --get_cap                             Get Capabilities
      --get_conn_cap <conn_num>             Get Connector Capability
      --get_conn_sts <conn_num>             Get Connector Status
      --get_cable_prop <conn_num>           Get Cable Properties
      --get_cur_cam <conn_num>              Get Current Alternate mode
      --get_alt_modes <conn_num> <conn|sop|sopprime|sopprimeprime>
                                            Get Alternate Modes
      --get_pdos <conn_num> <partner> <offset>
                 <src_snk> <type>           Get PDOS
      --set_uor <conn_num> <DFP/UFP/Accept> Set USB(Data) Operation Role
      --set_pdr <conn_num> <SRC/SNK/Accept> Set Power Direction Role
      --get_lpm_ppm_info <conn_num>         Get lpm and ppm info
      --get_error_sts <conn_num>            Get Error Status
      --set_ccom <conn_num> <Rd/Rp/DRP>     Set CC Operation Mode
      --set_new_cam <conn_num> <new_cam>
                 <am_specific> <enter|exit> Set New Current Alternate Mode
*/

#define FOR_ucsicontrol
#include "toys.h"

GLOBALS(
  int fp_command;
  int fp_response;
)

union attributes {
  struct {
    unsigned disabledstatesupport : 1;
    unsigned batterycharging : 1;
    unsigned usbpowerdelivery : 1;
    unsigned reserved1 : 3;
    unsigned usbtypeccurrent : 1;
    unsigned reserved2 : 1;
    union {
      struct {
        unsigned char acsupply : 1;
        unsigned char psreserved1 : 1;
        unsigned char other : 1;
        unsigned char psreserved2 : 3;
        unsigned char vbus : 1;
        unsigned char psreserved3 : 1;
      };
      unsigned char raw_powersrc;
    } bmPowerSource;
    unsigned reserved3 : 16;
  };
  unsigned raw_attrs;
};

union optionalfeature {
  struct {
    unsigned char setccomsupported : 1;
    unsigned char setpowerlevelsupported : 1;
    unsigned char altmodedetailssupported : 1;
    unsigned char altmodeoverridesupported : 1;
    unsigned char pdodetailssupported : 1;
    unsigned char cabledetailssupported : 1;
    unsigned char extsupplynotificationsupported : 1;
    unsigned char pdresetnotificationsupported : 1;
    unsigned char getpdmessagesupported : 1;
    unsigned char getattentionvdosupported : 1;
    unsigned char fwupdaterequestsupported : 1;
    unsigned char negotiatedpowerlevelchangesupported : 1;
    unsigned char securityrequestsupported : 1;
    unsigned char setretimermodesupported : 1;
    unsigned char chunkingsupportsupported : 1;
    unsigned char reserved1 : 1;
    unsigned char reserved2;
  };
  unsigned char raw_optfeas[3];
};

struct capability_data {
  union attributes bmAttributes;
  unsigned bNumConnectors : 7;
  unsigned reserved1 : 1;
  union optionalfeature bmOptionalFeatures;
  unsigned bNumAltModes : 8;
  unsigned reserved2 : 8;
  unsigned bcdBCVersion : 16;
  unsigned bcdPDVersion : 16;
  unsigned bcdTypeCVersion : 16;
};

union operationmode {
  struct {
    unsigned char rponly : 1;
    unsigned char rdonly : 1;
    unsigned char drp : 1;
    unsigned char analogaudioaccessorymode : 1;
    unsigned char debugaccessorymode : 1;
    unsigned char usb2 : 1;
    unsigned char usb3 : 1;
    unsigned char alternatemode : 1;
  };
  unsigned char raw_operationmode;
};

union extendedoperationmode {
  struct {
    unsigned char usb4gen2 : 1;
    unsigned char eprsrc : 1;
    unsigned char eprsnk : 1;
    unsigned char usb4gen3 : 1;
    unsigned char usb4gen4 : 1;
    unsigned char reserved : 3;
  };
  unsigned char raw_extendedoperationmode;
};

union miscellaneouscapabilities {
  struct {
    unsigned char fwupdate : 1;
    unsigned char security : 1;
    unsigned char reserved : 2;
  };
  unsigned char raw_miscellaneouscapabilities;
};

struct connector_cap_data {
  union operationmode opr_mode;
  unsigned provider : 1;
  unsigned consumer : 1;
  unsigned swap2dfp : 1;
  unsigned swap2ufp : 1;
  unsigned swap2src : 1;
  unsigned swap2snk : 1;
  unsigned extended_operation_mode : 8;
  unsigned miscellaneous_capabilities : 4;
  unsigned reverse_current_protection_support : 1;
  unsigned partner_pd_rev : 2;
  unsigned reserved : 3;
} __attribute__((packed));

union connectorstatuschange {
  struct {
    unsigned char reserved1 : 1;
    unsigned char ExternalSupplyChange : 1;
    unsigned char PowerOperationModechange : 1;
    unsigned char Attention : 1;
    unsigned char Reserved2 : 1;
    unsigned char SupportedProviderCapabilitiesChange : 1;
    unsigned char NegotiatedPowerLevelChange : 1;
    unsigned char PDResetComplete : 1;
    unsigned char SupportedCAMChange : 1;
    unsigned char BatteryChargingStatusChange : 1;
    unsigned char Reserved3 : 1;
    unsigned char ConnectorPartnerChanged : 1;
    unsigned char PowerDirectionChanged : 1;
    unsigned char SinkPathStatusChange : 1;
    unsigned char ConnectChange : 1;
    unsigned char Error : 1;
  };
  unsigned short raw_conn_stschang;
};

union connectorpartnerflags {
  struct {
    unsigned char usb : 1;
    unsigned char altmode : 1;
    unsigned char usb4_gen3 : 1;
    unsigned char usb4_gen4 : 1;
    unsigned char reserved : 4;
  };
  unsigned char raw_conn_part_flags;
};

struct connector_status {
  union connectorstatuschange ConnectorStatusChange;
  unsigned PowerOperationMode : 3;
  unsigned ConnectStatus : 1;
  unsigned PowerDirection : 1;
  unsigned ConnectorPartnerFlags : 8;
  unsigned ConnectorPartnerType : 3;
  unsigned RequestDataObject : 32;
  unsigned BatteryChargingCapabilityStatus : 2;
  unsigned ProviderCapabilitiesLimitedReason : 4;
  unsigned bcdPDVersionOperationMode : 16;
  unsigned Orientation : 1;
  unsigned SinkPathStatus : 1;
  unsigned ReverseCurrentProtectionStatus : 1;
  unsigned PowerReadingReady : 1;
  unsigned CurrentScale : 3;
  unsigned PeakCurrent : 16;
  unsigned AverageCurrent : 16;
  unsigned VoltageScale : 4;
  unsigned VoltageReading : 16;
  unsigned Reserved : 7;
} __attribute__((packed));

struct cable_property {
  unsigned short speed_supported;
  unsigned current_capability : 8;
  unsigned vbus_support : 1;
  unsigned cable_type : 1;
  unsigned directionality : 1;
  unsigned plug_end_type : 2;
  unsigned mode_support : 1;
  unsigned cable_pd_revision : 2;
  unsigned latency : 4;
  unsigned reserved : 28;
} __attribute__((packed));

struct get_lpm_ppm_info {
  unsigned short vid;
  unsigned short pid;
  unsigned xid;
  unsigned fw_version_upper;
  unsigned fw_version_lower;
  unsigned hw_version;
};

struct get_error_status {
  struct {
    unsigned short unrecognized_command : 1;
    unsigned short nonexistent_connector_number : 1;
    unsigned short invalid_cmd_specific_params : 1;
    unsigned short incompatible_connector_partner : 1;
    unsigned short cc_comm_error : 1;
    unsigned short cmd_unsuccessful_dead_battery : 1;
    unsigned short contract_negotiation_failure : 1;
    unsigned short overcurrent : 1;
    unsigned short undefined : 1;
    unsigned short port_partner_reject_swap : 1;
    unsigned short hard_reset : 1;
    unsigned short ppm_policy_conflict : 1;
    unsigned short swap_rejected : 1;
    unsigned short reverse_current_protection : 1;
    unsigned short set_sink_path_rejected : 1;
    unsigned short reserved : 1;
  } error_info;
  unsigned short vendor_defined;
} __attribute__((packed));

struct altmode_data {
  uint16_t svid;
  uint32_t vdo;
};

static int hex_to_int(char c)
{
  if (c >= '0' && c <= '9') return c-'0';
  if (c >= 'a' && c <= 'f') return c-'a'+10;
  if (c >= 'A' && c <= 'F') return c-'A'+10;

  return -1;
}

// Recursively search for "command" and "response" files under base.
static int find_ucsi_files(char *base, char *cmd_path, char *resp_path)
{
  struct dirent *dp;
  DIR *dir = opendir(base);
  int cmd_found = 0, resp_found = 0;
  char path[PATH_MAX];

  if (!dir) return -1;

  while ((dp = readdir(dir))) {
    if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;
    snprintf(path, sizeof(path), "%s/%s", base, dp->d_name);

    if (!strcmp(dp->d_name, "command")) {
      xstrncpy(cmd_path, path, PATH_MAX);
      cmd_found = 1;
    }
    if (!strcmp(dp->d_name, "response")) {
      xstrncpy(resp_path, path, PATH_MAX);
      resp_found = 1;
    }
    if (cmd_found && resp_found) {
      closedir(dir);

      return 0;
    }
    if (!find_ucsi_files(path, cmd_path, resp_path)) {
      closedir(dir);

      return 0;
    }
  }
  closedir(dir);

  return -1;
}

static void ucsi_open(void)
{
  char *base = "/sys/kernel/debug/usb/ucsi";
  char cmd_path[PATH_MAX] = {0}, resp_path[PATH_MAX] = {0};

  // The UCSI debugfs "command" and "response" files must both be present.
  if (find_ucsi_files(base, cmd_path, resp_path) ||
      access(cmd_path, F_OK) || access(resp_path, F_OK))
    error_exit("UCSI debugfs not present: no command/response file under %s",
      base);

  TT.fp_command = xopen(cmd_path, O_WRONLY);
  TT.fp_response = xopen(resp_path, O_RDONLY);
}

static void ucsi_close(void)
{
  if (TT.fp_command > 0) close(TT.fp_command);
  if (TT.fp_response > 0) close(TT.fp_response);
}

// Read a UCSI response, decode ASCII hex (skipping leading "0x"), and reverse
// the byte order. Returns number of decoded bytes, or -1 on error.
static int read_resp(unsigned char *data)
{
  char *c = toybuf;
  unsigned char *tmp = (void *)libbuf;
  int i, n, di = 0, total = 0;

  if (TT.fp_response <= 0) return -1;

  // Read the whole response; a single read() may not return all of it.
  while (total < 1024 &&
         (n = read(TT.fp_response, c+total, 1024-total)) > 0)
    total += n;
  lseek(TT.fp_response, 0, SEEK_SET);
  if (total <= 2) return -1;

  // Skip the leading "0x" and decode every hex pair in the response.
  for (i = 2; i+1 < total && di < 512; i += 2) {
    int hi = hex_to_int(c[i]), lo = hex_to_int(c[i+1]);

    if (hi < 0 || lo < 0) break;
    tmp[di++] = (hi<<4) | lo;
  }
  // Response is printed MSB-first; reverse to little-endian struct order.
  for (i = 0; i < di; i++) data[i] = tmp[di-1-i];

  return di;
}

// Write a preformatted command string and read back the response.
static int ucsi_xfer(char *cmd, unsigned char *resp)
{
  char buf[64] = {0};

  if (TT.fp_command <= 0) return -1;
  xstrncpy(buf, cmd, sizeof(buf));
  if (write(TT.fp_command, buf, sizeof(buf)) < 0) return -1;

  return read_resp(resp);
}

static void print_message_in(unsigned char *p, int len)
{
  int i;

  printf("UCSI MESSAGE_IN:\n==================\n");
  for (i = 0; i < len; i++) {
    printf("%02x ", p[i]);
    if (!((i+1)%8)) printf("\n");
  }
  printf("\n");
}

static void hex_to_decimal(unsigned version)
{
  printf("%x.%02x\n", (version>>8)&0xff, version&0xff);
}

static int get_conn(char *s)
{
  int conn = atolx(s);

  if (conn < 0) error_exit("Invalid connector number: %s", s);

  return conn;
}

static void print_capability(struct capability_data *c)
{
  printf("\nUCSI_GET_CAPABILITY_IN:\n-------------------------\n");
  printf("bmAttributes:\n");
  printf("  disabledStateSupport: %u\n", c->bmAttributes.disabledstatesupport);
  printf("  batteryCharging: %u\n", c->bmAttributes.batterycharging);
  printf("  usbPowerDelivery: %u\n", c->bmAttributes.usbpowerdelivery);
  printf("  usbTypeCCurrent: %u\n", c->bmAttributes.usbtypeccurrent);
  printf("  bmPowerSource.acsupply: %u\n",
    c->bmAttributes.bmPowerSource.acsupply);
  printf("  bmPowerSource.other: %u\n", c->bmAttributes.bmPowerSource.other);
  printf("  bmPowerSource.vbus: %u\n", c->bmAttributes.bmPowerSource.vbus);
  printf("bNumConnectors: %x\n", c->bNumConnectors);
  printf("bmOptionalFeatures:\n");
  printf("  setccomsupported: %u\n", c->bmOptionalFeatures.setccomsupported);
  printf("  setpowerlevelsupported: %u\n",
    c->bmOptionalFeatures.setpowerlevelsupported);
  printf("  altmodedetailssupported: %u\n",
    c->bmOptionalFeatures.altmodedetailssupported);
  printf("  altmodeoverridesupported: %u\n",
    c->bmOptionalFeatures.altmodeoverridesupported);
  printf("  pdodetailssupported: %u\n",
    c->bmOptionalFeatures.pdodetailssupported);
  printf("  cabledetailssupported: %u\n",
    c->bmOptionalFeatures.cabledetailssupported);
  printf("  extsupplynotificationsupported: %u\n",
    c->bmOptionalFeatures.extsupplynotificationsupported);
  printf("  pdresetnotificationsupported: %u\n",
    c->bmOptionalFeatures.pdresetnotificationsupported);
  printf("  getpdmessagesupported: %u\n",
    c->bmOptionalFeatures.getpdmessagesupported);
  printf("  getattentionvdosupported: %u\n",
    c->bmOptionalFeatures.getattentionvdosupported);
  printf("  fwupdaterequestsupported: %u\n",
    c->bmOptionalFeatures.fwupdaterequestsupported);
  printf("  negotiatedpowerlevelchangesupported: %u\n",
    c->bmOptionalFeatures.negotiatedpowerlevelchangesupported);
  printf("  securityrequestsupported: %u\n",
    c->bmOptionalFeatures.securityrequestsupported);
  printf("  setretimermodesupported: %u\n",
    c->bmOptionalFeatures.setretimermodesupported);
  printf("  chunkingsupportsupported: %u\n",
    c->bmOptionalFeatures.chunkingsupportsupported);
  printf("bNumAltModes: %x\n", c->bNumAltModes);
  printf("bcdBCVersion: ");
  hex_to_decimal(c->bcdBCVersion);
  printf("bcdPDVersion: ");
  hex_to_decimal(c->bcdPDVersion);
  printf("bcdTypeCVersion: ");
  hex_to_decimal(c->bcdTypeCVersion);
}

static void print_connector_capability(struct connector_cap_data *c)
{
  union extendedoperationmode eom;
  union miscellaneouscapabilities mc;

  printf("\nGET_CONNECTOR_CAPABILITY:\n-------------------------\n");
  printf("OperationMode: 0x%x\n", c->opr_mode.raw_operationmode);
  printf("  Rponly: %d\n", c->opr_mode.rponly);
  printf("  Rdonly: %d\n", c->opr_mode.rdonly);
  printf("  Drp: %d\n", c->opr_mode.drp);
  printf("  AnalogAudioAccessorymode: %d\n",
    c->opr_mode.analogaudioaccessorymode);
  printf("  DebugAccessorymode: %d\n", c->opr_mode.debugaccessorymode);
  printf("  Usb2: %d\n", c->opr_mode.usb2);
  printf("  Usb3: %d\n", c->opr_mode.usb3);
  printf("  AlternateMode: %d\n", c->opr_mode.alternatemode);
  printf("Provider: %d\n", c->provider);
  printf("Consumer: %d\n", c->consumer);
  printf("SwapToDfp: %d\n", c->swap2dfp);
  printf("SwapToUfp: %d\n", c->swap2ufp);
  printf("SwapToSrc: %d\n", c->swap2src);
  printf("SwapToSnk: %d\n", c->swap2snk);
  printf("ExtendedOperationMode: 0x%x\n", c->extended_operation_mode);
  eom.raw_extendedoperationmode = c->extended_operation_mode;
  printf("  Usb4Gen2: %d\n", eom.usb4gen2);
  printf("  EprSrc: %d\n", eom.eprsrc);
  printf("  EprSnk: %d\n", eom.eprsnk);
  printf("  Usb4Gen3: %d\n", eom.usb4gen3);
  printf("  Usb4Gen4: %d\n", eom.usb4gen4);
  printf("MiscellaneousCapabilities: 0x%x\n", c->miscellaneous_capabilities);
  mc.raw_miscellaneouscapabilities = c->miscellaneous_capabilities;
  printf("  FwUpdate: %d\n", mc.fwupdate);
  printf("  Security: %d\n", mc.security);
  printf("ReverseCurrentProtectionSupport: %d\n",
    c->reverse_current_protection_support);
  printf("PartnerPDRevision: %d\n", c->partner_pd_rev);
}

static void print_connector_status(struct connector_status *c)
{
  union connectorpartnerflags cpf;

  printf("\nUCSI_GET_CONNECTOR_STATUS:\n-------------------------\n");
  printf("ConnectorStatusChange: 0x%x\n",
    c->ConnectorStatusChange.raw_conn_stschang);
  printf("  ExternalSupplyChange: %d\n",
    c->ConnectorStatusChange.ExternalSupplyChange);
  printf("  Attention: %d\n", c->ConnectorStatusChange.Attention);
  printf("  SupportedProviderCapabilitiesChange: %d\n",
    c->ConnectorStatusChange.SupportedProviderCapabilitiesChange);
  printf("  NegotiatedPowerLevelChange: %d\n",
    c->ConnectorStatusChange.NegotiatedPowerLevelChange);
  printf("  PDResetComplete: %d\n", c->ConnectorStatusChange.PDResetComplete);
  printf("  SupportedCAMChange: %d\n",
    c->ConnectorStatusChange.SupportedCAMChange);
  printf("  BatteryChargingStatusChange: %d\n",
    c->ConnectorStatusChange.BatteryChargingStatusChange);
  printf("  ConnectorPartnerChanged: %d\n",
    c->ConnectorStatusChange.ConnectorPartnerChanged);
  printf("  PowerDirectionChanged: %d\n",
    c->ConnectorStatusChange.PowerDirectionChanged);
  printf("  SinkPathStatusChange: %d\n",
    c->ConnectorStatusChange.SinkPathStatusChange);
  printf("  ConnectChange: %d\n", c->ConnectorStatusChange.ConnectChange);
  printf("  Error: %d\n", c->ConnectorStatusChange.Error);
  printf("PowerOperationMode: %d\n", c->PowerOperationMode);
  printf("  UsbDefaultOperation: %d\n", c->PowerOperationMode == 1);
  printf("  BC: %d\n", c->PowerOperationMode == 2);
  printf("  PD: %d\n", c->PowerOperationMode == 3);
  printf("  UsbTypecCurrent1.5A: %d\n", c->PowerOperationMode == 4);
  printf("  UsbTypecCurrent3A: %d\n", c->PowerOperationMode == 5);
  printf("  UsbTypecCurrent5A: %d\n", c->PowerOperationMode == 6);
  printf("ConnectStatus: %d\n", c->ConnectStatus);
  printf("PowerDirection: %d\n", c->PowerDirection);
  printf("  Consumer: %d\n", c->PowerDirection == 0);
  printf("  Provider: %d\n", c->PowerDirection == 1);
  printf("ConnectorPartnerFlags: 0x%x\n", c->ConnectorPartnerFlags);
  cpf.raw_conn_part_flags = c->ConnectorPartnerFlags;
  printf("  Usb: %d\n", cpf.usb);
  printf("  Dp: %d\n", cpf.altmode);
  printf("  Tbt: %d\n", cpf.usb4_gen3);
  printf("  Usb4: %d\n", cpf.usb4_gen4);
  printf("ConnectorPartnerType: %d\n", c->ConnectorPartnerType);
  printf("  DFPattached: %d\n", c->ConnectorPartnerType == 1);
  printf("  UFPattached: %d\n", c->ConnectorPartnerType == 2);
  printf("  PoweredCableNoUFPattached: %d\n", c->ConnectorPartnerType == 3);
  printf("  PoweredCableUFPattached: %d\n", c->ConnectorPartnerType == 4);
  printf("  DebugAccessoryattched: %d\n", c->ConnectorPartnerType == 5);
  printf("  AudioAccessoryAttached: %d\n", c->ConnectorPartnerType == 6);
  printf("RequestDataObject: 0x%x\n", c->RequestDataObject);
  printf("BatteryChargingCapabilityStatus: %d\n",
    c->BatteryChargingCapabilityStatus);
  printf("  NotCharging: %d\n", c->BatteryChargingCapabilityStatus == 0);
  printf("  NominalChargingRate: %d\n",
    c->BatteryChargingCapabilityStatus == 1);
  printf("  SlowChargingRate: %d\n", c->BatteryChargingCapabilityStatus == 2);
  printf("  VerySlowCharingRate: %d\n",
    c->BatteryChargingCapabilityStatus == 3);
  printf("ProviderCapabilitiesLimitedReason: %d\n",
    c->ProviderCapabilitiesLimitedReason);
  printf("bcdPDVersionOperationMode: ");
  hex_to_decimal(c->bcdPDVersionOperationMode);
  printf("Orientation: %d\n", c->Orientation);
  printf("  DirectOrientation : %d\n", c->Orientation == 0);
  printf("  FlippedOrientation : %d\n", c->Orientation == 1);
  printf("SinkPathStatus: %d\n", c->SinkPathStatus);
  printf("ReverseCurrentProtectionStatus: %d\n",
    c->ReverseCurrentProtectionStatus);
  printf("PowerReadingReady: %d\n", c->PowerReadingReady);
  printf("CurrentScale: %d\n", c->CurrentScale);
  printf("PeakCurrent: %d\n", c->PeakCurrent);
  printf("AverageCurrent: %d\n", c->AverageCurrent);
  printf("VoltageScale: %d\n", c->VoltageScale);
  printf("VoltageReading: %d\n", c->VoltageReading);
  printf("Voltage: %d\n", c->VoltageReading * c->VoltageScale * 5);
}

static void print_cable_property(struct cable_property *c)
{
  printf("\nGET_CABLE_PROPERTY:\n-------------------------\n");
  printf("bmSpeedSupported: 0x%x\n", c->speed_supported);
  printf("  Bits/s: %d\n", (c->speed_supported & 0x3) == 0);
  printf("  Kb/s: %d\n", (c->speed_supported & 0x3) == 1);
  printf("  Mb/s: %d\n", (c->speed_supported & 0x3) == 2);
  printf("  Gb/s: %d\n", (c->speed_supported & 0x3) == 3);
  printf("bCurrentCapability: %d mA\n", c->current_capability * 50);
  printf("VBUSInCable: %d\n", c->vbus_support);
  printf("CableType: %d\n", c->cable_type);
  printf("  PassiveCable: %d\n", c->cable_type == 0);
  printf("  ActiveCable: %d\n", c->cable_type == 1);
  printf("Directionality: %d\n", c->directionality);
  printf("PlugEndType: %d\n", c->plug_end_type);
  printf("  USBtypeA: %d\n", c->plug_end_type == 0);
  printf("  USBtypeB: %d\n", c->plug_end_type == 1);
  printf("  USBtypeC: %d\n", c->plug_end_type == 2);
  printf("  Other: %d\n", c->plug_end_type == 3);
  printf("ModeSupport: %d\n", c->mode_support);
  printf("CablePDRevision: %d\n", c->cable_pd_revision);
  printf("Latency: %d\n", c->latency);
}

static void print_lpm_ppm_info(struct get_lpm_ppm_info *c)
{
  printf("\nGET_LPM_PPM_INFO :\n-------------------------\n");
  printf("VID: 0x%x\n", c->vid);
  printf("PID: 0x%x\n", c->pid);
  printf("XID: 0x%x\n", c->xid);
  printf("FW Ver: %u.%u\n", c->fw_version_upper, c->fw_version_lower);
  printf("HW Ver: %u\n", c->hw_version);
}

static void print_error_status(struct get_error_status *c)
{
  printf("\nGET_ERROR_STATUS :\n-------------------------\n");
  printf("ErrorInformation:\n");
  printf("  UnrecognizedCmd: %d\n", c->error_info.unrecognized_command);
  printf("  NonExistentConnectorNum: %d\n",
    c->error_info.nonexistent_connector_number);
  printf("  InvalidCmdSpecificParam: %d\n",
    c->error_info.invalid_cmd_specific_params);
  printf("  IncompatibleConnectorPartner: %d\n",
    c->error_info.incompatible_connector_partner);
  printf("  CCcommunicationError: %d\n", c->error_info.cc_comm_error);
  printf("  CmdUnsuccessDeadBattery: %d\n",
    c->error_info.cmd_unsuccessful_dead_battery);
  printf("  ContractNegotiationFailure: %d\n",
    c->error_info.contract_negotiation_failure);
  printf("  OverCurrent: %d\n", c->error_info.overcurrent);
  printf("  Undefined: %d\n", c->error_info.undefined);
  printf("  PortPartnerRejectSwap: %d\n",
    c->error_info.port_partner_reject_swap);
  printf("  HardReset: %d\n", c->error_info.hard_reset);
  printf("  PpmPolicyConflict: %d\n", c->error_info.ppm_policy_conflict);
  printf("  SwapRejected: %d\n", c->error_info.swap_rejected);
  printf("  ReverseCurrentProtection: %d\n",
    c->error_info.reverse_current_protection);
  printf("  SetSinkPathRejected: %d\n", c->error_info.set_sink_path_rejected);
  printf("VendorDefined: 0x%x\n", c->vendor_defined);
}

// Map a string role to its UCSI code, or error out.
static int role_code(char *s, char *a, char *b, char *c, int va, int vb, int vc)
{
  if (!strcmp(s, a)) return va;
  if (!strcmp(s, b)) return vb;
  if (!strcmp(s, c)) return vc;
  error_exit("Invalid type: %s", s);
}

void ucsicontrol_main(void)
{
  char **args = toys.optargs, *op = args[0];
  unsigned char buf[256] = {0};
  int conn, n;

  // Accept both "--option" and "-h" style; show help when asked or no command.
  if (!op || !strcmp(op, "-h") || !strcmp(op, "--help")) {
    show_help(HELP_HEADER);
    xexit();
  }
  if (*op == '-') op += (op[1] == '-') ? 2 : 1;

  ucsi_open();

  if (!strcmp(op, "get_cap")) {
    if ((n = ucsi_xfer("6", buf)) < 16) error_exit("get_capability failed");
    print_message_in(buf, n);
    print_capability((void *)buf);
  } else if (!strcmp(op, "get_conn_cap")) {
    if (!args[1]) error_exit("get_conn_cap needs <conn_num>");
    conn = get_conn(args[1]);
    sprintf(toybuf, "0x%x", (conn+1)<<16 | 0x7);
    if ((n = ucsi_xfer(toybuf, buf)) < 16) error_exit("get_conn_cap failed");
    print_message_in(buf, n);
    print_connector_capability((void *)buf);
  } else if (!strcmp(op, "get_conn_sts")) {
    if (!args[1]) error_exit("get_conn_sts needs <conn_num>");
    conn = get_conn(args[1]);
    sprintf(toybuf, "0x%x", (conn+1)<<16 | 0x12);
    if ((n = ucsi_xfer(toybuf, buf)) < 16) error_exit("get_conn_sts failed");
    print_message_in(buf, sizeof(struct connector_status));
    print_connector_status((void *)buf);
  } else if (!strcmp(op, "get_cable_prop")) {
    if (!args[1]) error_exit("get_cable_prop needs <conn_num>");
    conn = get_conn(args[1]);
    sprintf(toybuf, "0x%x", (conn+1)<<16 | 0x11);
    if ((n = ucsi_xfer(toybuf, buf)) < 16) error_exit("get_cable_prop failed");
    print_message_in(buf, sizeof(struct cable_property));
    print_cable_property((void *)buf);
  } else if (!strcmp(op, "get_cur_cam")) {
    if (!args[1]) error_exit("get_cur_cam needs <conn_num>");
    conn = get_conn(args[1]);
    sprintf(toybuf, "0x%x", (conn+1)<<16 | 0x0E);
    if ((n = ucsi_xfer(toybuf, buf)) < 16) error_exit("get_cur_cam failed");
    print_message_in(buf, n);
    printf("\nGET_CURRENT_CAM :\n-------------------------\n");
    printf("CurrentAlternateMode: 0x%x\n", buf[0]);
  } else if (!strcmp(op, "get_alt_modes")) {
    int recipient, i = 0, prevn = 0;
    unsigned short first_svid = 0;
    unsigned char prev[256];

    if (!args[1] || !args[2])
      error_exit("Usage: get_alt_modes <conn_num> "
        "[conn|sop|sopprime|sopprimeprime]");
    conn = get_conn(args[1]);
    if (!strcmp(args[2], "conn")) recipient = 0;
    else if (!strcmp(args[2], "sop")) recipient = 1;
    else if (!strcmp(args[2], "sopprime")) recipient = 2;
    else if (!strcmp(args[2], "sopprimeprime")) recipient = 3;
    else error_exit("Usage: get_alt_modes <conn_num> "
      "<conn|sop|sopprime|sopprimeprime>");
    for (;;) {
      // GET_ALTERNATE_MODES (0x0C): Recipient[16], ConnNum[24],
      // AltModeOffset[32], NumAltModes[40]=1 -> up to 2 modes per call.
      unsigned long long cmd = 0x0cULL
        | ((unsigned long long)(recipient & 0x7)  << 16)
        | ((unsigned long long)((conn+1) & 0x7f) << 24)
        | ((unsigned long long)(i & 0xff)        << 32)
        | (1ULL << 40);
      unsigned short svid0, svid1;
      unsigned mid0, mid1;

      sprintf(toybuf, "0x%llx", cmd);
      // An exhausted recipient offset returns an empty/short response.
      if ((n = ucsi_xfer(toybuf, buf)) < 16) break;
      // Some LPMs are not UCSI-compliant at the end of the alternate mode
      // list: instead of returning a zero/short response, the chip replays the
      // previous MESSAGE_IN and only refreshes the leading SVID field (bytes
      // 0-1) with stale/garbage, leaving mid0, the second mode field and the
      // trailing bytes identical. A real next-offset response would carry a
      // different MID, so treat "everything past svid0 is unchanged" as the end
      // of the list and stop before printing the phantom mode.
      if (i && n == prevn && !memcmp(buf + 2, prev + 2, n - 2)) break;
      memcpy(prev, buf, n);
      prevn = n;

      // Table 6-26 response: SVID[0]@0 MID[0]@2 SVID[1]@6 MID[1]@8
      svid0 = buf[1]<<8 | buf[0];
      mid0  = buf[5]<<24 | buf[4]<<16 | buf[3]<<8 | buf[2];
      svid1 = buf[7]<<8 | buf[6];
      mid1  = buf[11]<<24 | buf[10]<<16 | buf[9]<<8 | buf[8];

      if (!svid0) break;
      // LPM repeats the same response when offset is exhausted; stop on wrap.
      if (first_svid && svid0 == first_svid) break;
      print_message_in(buf, n);
      if (!first_svid) {
        first_svid = svid0;
        printf("\nGET_ALTERNATE_MODE :\n-------------------------\n");
      }
      printf("Mode [%d]\n  svid: 0x%x\n  vdo : 0x%x\n", i, svid0, mid0);
      i++;

      if (!svid1 || svid1 == first_svid) break;
      printf("Mode [%d]\n  svid: 0x%x\n  vdo : 0x%x\n", i, svid1, mid1);
      i++;
    }
    if (!first_svid)
      printf("\nGET_ALTERNATE_MODE :\n-------------------------\n"
        "No alternate modes reported\n");
  } else if (!strcmp(op, "get_pdos")) {
    int partner, offset, srcsnk, type, i = 0;
    unsigned ppdo = 0, pdo;

    if (!args[1] || !args[2] || !args[3] || !args[4] || !args[5])
      error_exit("get_pdos needs <conn_num> <partner> <offset> <src_snk> "
        "<type>");
    conn = get_conn(args[1]);
    partner = atolx(args[2]);
    offset = atolx(args[3]);
    srcsnk = atolx(args[4]);
    type = atolx(args[5]);
    printf("\nGET_PDOS :\n-------------------------\n");
    for (;;) {
      unsigned long long cmd = 0x10 | ((unsigned long long)(conn+1)<<16)
        | ((unsigned long long)(partner&1)<<23)
        | ((unsigned long long)((offset+i)&0xff)<<24)
        | ((unsigned long long)(srcsnk&1)<<34)
        | ((unsigned long long)(type&3)<<35);

      sprintf(toybuf, "0x%llx", cmd);
      if ((n = ucsi_xfer(toybuf, buf)) < 16) break;
      pdo = buf[3]<<24 | buf[2]<<16 | buf[1]<<8 | buf[0];
      if (!pdo || pdo == ppdo) break;
      printf("PDO [%d]: 0x%x\n", i, pdo);
      ppdo = pdo;
      i++;
    }
  } else if (!strcmp(op, "get_lpm_ppm_info")) {
    if (!args[1]) error_exit("get_lpm_ppm_info needs <conn_num>");
    conn = get_conn(args[1]);
    sprintf(toybuf, "0x%x", (conn+1)<<16 | 0x22);
    if ((n = ucsi_xfer(toybuf, buf)) < 16)
      error_exit("get_lpm_ppm_info failed");
    print_message_in(buf, n);
    print_lpm_ppm_info((void *)buf);
  } else if (!strcmp(op, "get_error_sts")) {
    if (!args[1]) error_exit("get_error_sts needs <conn_num>");
    conn = get_conn(args[1]);
    sprintf(toybuf, "0x%x", (conn+1)<<16 | 0x13);
    if ((n = ucsi_xfer(toybuf, buf)) < 16) error_exit("get_error_sts failed");
    print_message_in(buf, n);
    print_error_status((void *)buf);
  } else if (!strcmp(op, "conn_rst")) {
    int rst;

    if (!args[1] || !args[2])
      error_exit("conn_rst needs <conn_num> <soft|hard>");
    conn = get_conn(args[1]);
    rst = role_code(args[2], "soft", "hard", "", 0, 1, -1);
    sprintf(toybuf, "0x%x", 0x03 | ((conn+1)<<16) | (rst<<23));
    n = ucsi_xfer(toybuf, buf);
    printf("connector%d %s reset%s\n", conn, rst ? "hard" : "soft",
      n < 16 ? " failed" : "");
  } else if (!strcmp(op, "set_uor")) {
    int uor;

    if (!args[1] || !args[2])
      error_exit("set_uor needs <conn_num> <DFP|UFP|Accept>");
    conn = get_conn(args[1]);
    uor = role_code(args[2], "DFP", "UFP", "Accept", 1, 2, 4);
    sprintf(toybuf, "0x%x", 0x09 | ((conn+1)<<16) | ((0x4|uor)<<23));
    n = ucsi_xfer(toybuf, buf);
    printf("connector%d set data role operation %s%s\n", conn, args[2],
      n < 16 ? " failed" : "");
  } else if (!strcmp(op, "set_pdr")) {
    int pdr;

    if (!args[1] || !args[2])
      error_exit("set_pdr needs <conn_num> <SRC|SNK|Accept>");
    conn = get_conn(args[1]);
    pdr = role_code(args[2], "SRC", "SNK", "Accept", 1, 2, 4);
    sprintf(toybuf, "0x%x", 0x0B | ((conn+1)<<16) | (pdr<<23));
    n = ucsi_xfer(toybuf, buf);
    printf("connector%d power role swap operation %s%s\n", conn, args[2],
      n < 16 ? " failed" : "");
  } else if (!strcmp(op, "set_ccom")) {
    int ccom;

    if (!args[1] || !args[2])
      error_exit("set_ccom needs <conn_num> <Rd|Rp|DRP>");
    conn = get_conn(args[1]);
    ccom = role_code(args[2], "Rd", "Rp", "DRP", 1, 2, 4);
    sprintf(toybuf, "0x%x", 0x08 | ((conn+1)<<16) | (ccom<<23));
    n = ucsi_xfer(toybuf, buf);
    printf("connector%d set cc operation mode %s%s\n", conn, args[2],
      n < 16 ? " failed" : "");
  } else if (!strcmp(op, "set_new_cam")) {
    int ee, newcam;
    unsigned amspec;

    if (!args[1] || !args[2] || !args[3] || !args[4])
      error_exit("set_new_cam needs <conn_num> <new_cam> <am_specific> "
        "<enter|exit>");
    conn = get_conn(args[1]);
    newcam = atolx(args[2]);
    amspec = atolx(args[3]);
    ee = role_code(args[4], "enter", "exit", "", 1, 0, -1);
    sprintf(toybuf, "0x%lx", 0x0FUL | ((unsigned long)(conn+1)<<16)
      | ((unsigned long)(ee&1)<<23) | ((unsigned long)(newcam&0xff)<<24)
      | ((unsigned long)amspec<<32));
    n = ucsi_xfer(toybuf, buf);
    printf("connector%d set new cam %s%s\n", conn, args[4],
      n < 16 ? " failed" : "");
  } else {
    ucsi_close();
    error_exit("Unknown command: %s", op);
  }

  ucsi_close();
}
