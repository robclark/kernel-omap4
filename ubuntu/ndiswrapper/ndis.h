/*
 *  Copyright (C) 2003-2005 Pontus Fuchs, Giridhar Pemmasani
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 */

#ifndef _NDIS_H_
#define _NDIS_H_

#include "ntoskernel.h"

//#define ALLOW_POOL_OVERFLOW 1

#define NDIS_DMA_24BITS 0
#define NDIS_DMA_32BITS 1
#define NDIS_DMA_64BITS 2

#ifdef CONFIG_X86_64
#define MAXIMUM_PROCESSORS  64
#else
#define MAXIMUM_PROCESSORS  32
#endif

typedef UINT NDIS_STATUS;
typedef UCHAR NDIS_DMA_SIZE;
typedef LONG ndis_rssi;
typedef ULONG ndis_key_index;
typedef ULONG ndis_tx_power_level;
typedef ULONGULONG ndis_key_rsc;
typedef UCHAR ndis_rates[NDIS_MAX_RATES];
typedef UCHAR ndis_rates_ex[NDIS_MAX_RATES_EX];
typedef UCHAR mac_address[ETH_ALEN];
typedef ULONG ndis_fragmentation_threshold;
typedef ULONG ndis_rts_threshold;
typedef ULONG ndis_antenna;
typedef ULONG ndis_oid;

typedef UCHAR ndis_pmkid_vavlue[16];

typedef uint64_t NDIS_PHY_ADDRESS;

struct ndis_sg_element {
	PHYSICAL_ADDRESS address;
	ULONG length;
	ULONG_PTR reserved;
};

struct ndis_sg_list {
	ULONG nent;
	ULONG_PTR reserved;
	struct ndis_sg_element elements[];
};

/* when sending packets, ndiswrapper associates exactly one sg element
 * in sg list */
struct wrap_tx_sg_list {
	ULONG nent;
	ULONG_PTR reserved;
	struct ndis_sg_element elements[1];
};

struct ndis_phy_addr_unit {
	NDIS_PHY_ADDRESS phy_addr;
	UINT length;
};

typedef struct mdl ndis_buffer;

struct ndis_buffer_pool {
	ndis_buffer *free_descr;
//	NT_SPIN_LOCK lock;
	spinlock_t lock;
	UINT max_descr;
	UINT num_allocated_descr;
};

#define NDIS_PROTOCOL_ID_DEFAULT	0x00
#define NDIS_PROTOCOL_ID_TCP_IP		0x02
#define NDIS_PROTOCOL_ID_IPX		0x06
#define NDIS_PROTOCOL_ID_NBF		0x07
#define NDIS_PROTOCOL_ID_MAX		0x0F
#define NDIS_PROTOCOL_ID_MASK		0x0F

#define fPACKET_WRAPPER_RESERVED		0x3F
#define fPACKET_CONTAINS_MEDIA_SPECIFIC_INFO	0x40
#define fPACKET_ALLOCATED_BY_NDIS		0x80

#define PROTOCOL_RESERVED_SIZE_IN_PACKET (4 * sizeof(void *))

struct transport_header_offset {
	USHORT protocol_type;
	USHORT header_offset;
};

struct ndis_network_address {
	USHORT length;
	USHORT type;
	UCHAR address[1];
};

struct ndis_network_address_list {
	LONG count;
	USHORT type;
	struct ndis_network_address address[1];
};

struct ndis_tcp_ip_checksum_packet_info {
	union {
		struct {
			ULONG v4:1;
			ULONG v6:1;
			ULONG tcp:1;
			ULONG udp:1;
			ULONG ip:1;
		} tx;
		struct {
			ULONG tcp_failed:1;
			ULONG udp_failed:1;
			ULONG ip_failed:1;
			ULONG tcp_succeeded:1;
			ULONG udp_succeeded:1;
			ULONG ip_succeeded:1;
			ULONG loopback:1;
		} rx;
		ULONG value;
	};
};

enum ndis_task {
	TcpIpChecksumNdisTask, IpSecNdisTask, TcpLargeSendNdisTask, MaxNdisTask
};

enum ndis_encapsulation {
	UNSPECIFIED_Encapsulation, NULL_Encapsulation,
	IEEE_802_3_Encapsulation, IEEE_802_5_Encapsulation,
	LLC_SNAP_ROUTED_Encapsulation, LLC_SNAP_BRIDGED_Encapsulation
};

#define NDIS_TASK_OFFLOAD_VERSION 1

struct ndis_encapsulation_format {
	enum ndis_encapsulation encap;
	struct {
		ULONG fixed_header_size:1;
		ULONG reserved:31;
	} flags;
	ULONG header_size;
};

struct ndis_task_offload_header {
	ULONG version;
	ULONG size;
	ULONG reserved;
	ULONG offset_first_task;
	struct ndis_encapsulation_format encap_format;
};

struct ndis_task_offload {
	ULONG version;
	ULONG size;
	enum ndis_task task;
	ULONG offset_next_task;
	ULONG task_buf_length;
	UCHAR task_buf[1];
};

struct v4_checksum {
	union {
		struct {
			ULONG ip_opts:1;
			ULONG tcp_opts:1;
			ULONG tcp_csum:1;
			ULONG udp_csum:1;
			ULONG ip_csum:1;
		};
		ULONG value;
	};
};

struct v6_checksum {
	ULONG ip_supported:1;
	ULONG tcp_supported:1;
	ULONG tcp_csum:1;
	ULONG udp_csum:1;
};

struct ndis_task_tcp_ip_checksum {
	struct v4_checksum v4_tx;
	struct v4_checksum v4_rx;
	struct v6_checksum v6_tx;
	struct v6_checksum v6_rx;
};

struct ndis_task_tcp_large_send {
	ULONG version;
	ULONG max_size;
	ULONG min_seg_count;
	BOOLEAN tcp_opts;
	BOOLEAN ip_opts;
};

struct ndis_packet;

struct ndis_packet_pool {
	struct ndis_packet *free_descr;
//	NT_SPIN_LOCK lock;
	spinlock_t lock;
	UINT max_descr;
	UINT num_allocated_descr;
	UINT num_used_descr;
	UINT proto_rsvd_length;
};

struct ndis_packet_stack {
	ULONG_PTR IM_reserved[2];
	ULONG_PTR ndis_reserved[4];
};

enum ndis_per_packet_info {
	TcpIpChecksumPacketInfo, IpSecPacketInfo, TcpLargeSendPacketInfo,
	ClassificationHandlePacketInfo, NdisReserved,
	ScatterGatherListPacketInfo, Ieee8021QInfo, OriginalPacketInfo,
	PacketCancelId, MaxPerPacketInfo
};

struct ndis_packet_extension {
	void *info[MaxPerPacketInfo];
};

struct ndis_packet_private {
	UINT nr_pages;
	UINT len;
	ndis_buffer *buffer_head;
	ndis_buffer *buffer_tail;
	struct ndis_packet_pool *pool;
	UINT count;
	ULONG flags;
	BOOLEAN valid_counts;
	UCHAR packet_flags;
	USHORT oob_offset;
};

struct ndis_packet {
	struct ndis_packet_private private;
	/* for use by miniport */
	union {
		/* for connectionless mininports */
		struct {
			UCHAR miniport_reserved[2 * sizeof(void *)];
			UCHAR wrapper_reserved[2 * sizeof(void *)];
		} cl_reserved;
		/* for deserialized miniports */
		struct {
			UCHAR miniport_reserved_ex[3 * sizeof(void *)];
			UCHAR wrapper_reserved_ex[sizeof(void *)];
		} deserailized_reserved;
		struct {
			UCHAR mac_reserved[4 * sizeof(void *)];
		} mac_reserved;
	};
	ULONG_PTR reserved[2];
	UCHAR protocol_reserved[1];
};

/* OOB data */
struct ndis_packet_oob_data {
	union {
		ULONGLONG time_to_tx;
		ULONGLONG time_txed;
	};
	ULONGLONG time_rxed;
	UINT header_size;
	UINT media_size;
	void *media;
	NDIS_STATUS status;

	/* ndiswrapper specific info; extension should be right after
	 * ndis's oob_data */
	struct ndis_packet_extension ext;
	union {
		/* used for tx only */
		struct {
			struct sk_buff *tx_skb;
			union {
				struct wrap_tx_sg_list wrap_tx_sg_list;
				struct ndis_sg_list *tx_sg_list;
			};
		};
		/* used for rx only */
		struct {
			unsigned char header[ETH_HLEN];
			unsigned char *look_ahead;
			UINT look_ahead_size;
		};
	};
};

#define NDIS_PACKET_OOB_DATA(packet)					\
	(struct ndis_packet_oob_data *)(((void *)(packet)) +		\
					(packet)->private.oob_offset)

enum ndis_device_pnp_event {
	NdisDevicePnPEventQueryRemoved, NdisDevicePnPEventRemoved,
	NdisDevicePnPEventSurpriseRemoved, NdisDevicePnPEventQueryStopped,
	NdisDevicePnPEventStopped, NdisDevicePnPEventPowerProfileChanged,
	NdisDevicePnPEventMaximum
};

enum ndis_request_type {
	NdisRequestQueryInformation, NdisRequestSetInformation,
	NdisRequestQueryStatistics, NdisRequestOpen, NdisRequestClose,
	NdisRequestSend, NdisRequestTransferData, NdisRequestReset,
	NdisRequestGeneric1, NdisRequestGeneric2, NdisRequestGeneric3,
	NdisRequestGeneric4
};

struct ndis_request {
	mac_address mac;
	enum ndis_request_type request_type;
	union data {
		struct query_info {
			UINT oid;
			void *buf;
			UINT buf_len;
			UINT written;
			UINT needed;
		} query_info;
		struct set_info {
			UINT oid;
			void *buf;
			UINT buf_len;
			UINT written;
			UINT needed;
		} set_info;
	} data;
};

enum ndis_medium {
	NdisMedium802_3, NdisMedium802_5, NdisMediumFddi, NdisMediumWan,
	NdisMediumLocalTalk, NdisMediumDix, NdisMediumArcnetRaw,
	NdisMediumArcnet878_2, NdisMediumAtm, NdisMediumWirelessWan,
	NdisMediumIrda, NdisMediumBpc, NdisMediumCoWan,
	NdisMedium1394, NdisMediumMax
};

enum ndis_physical_medium {
	NdisPhysicalMediumUnspecified, NdisPhysicalMediumWirelessLan,
	NdisPhysicalMediumCableModem, NdisPhysicalMediumPhoneLine,
	NdisPhysicalMediumPowerLine, NdisPhysicalMediumDSL,
	NdisPhysicalMediumFibreChannel, NdisPhysicalMedium1394,
	NdisPhysicalMediumWirelessWan, NdisPhysicalMediumMax
};

enum ndis_power_state {
	NdisDeviceStateUnspecified = 0,
	NdisDeviceStateD0, NdisDeviceStateD1, NdisDeviceStateD2,
	NdisDeviceStateD3, NdisDeviceStateMaximum
};

enum ndis_power_profile {
	NdisPowerProfileBattery, NdisPowerProfileAcOnLine
};

struct ndis_pm_wakeup_capabilities {
	enum ndis_power_state min_magic_packet_wakeup;
	enum ndis_power_state min_pattern_wakeup;
	enum ndis_power_state min_link_change_wakeup;
};

#define NDIS_PNP_WAKE_UP_MAGIC_PACKET			0x00000001
#define NDIS_PNP_WAKE_UP_PATTERN_MATCH			0x00000002
#define NDIS_PNP_WAKE_UP_LINK_CHANGE			0x00000004

enum net_pnp_event_code {
	NetEventSetPower, NetEventQueryPower, NetEventQueryRemoveDevice,
	NetEventCancelRemoveDevice, NetEventReconfigure, NetEventBindList,
	NetEventBindsComplete, NetEventPnPCapabilities, NetEventMaximum
};

struct net_pnp_event {
	enum net_pnp_event_code code;
	void *buf;
	ULONG buf_length;
	ULONG_PTR ndis_reserved[4];
	ULONG_PTR transport_reserved[4];
	ULONG_PTR tdi_reserved[4];
	ULONG_PTR tdi_client_reserved[4];
};

struct ndis_pnp_capabilities {
	ULONG flags;
	struct ndis_pm_wakeup_capabilities wakeup;
};

typedef void (*ndis_isr_handler)(BOOLEAN *recognized, BOOLEAN *queue_handler,
				 void *handle) wstdcall;
typedef void (*ndis_interrupt_handler)(void *ctx) wstdcall;

struct miniport {
	/* NDIS 3.0 */
	UCHAR major_version;
	UCHAR minor_version;
	USHORT filler;
	UINT reserved;
	BOOLEAN (*hangcheck)(void *ctx) wstdcall;
	void (*disable_interrupt)(void *ctx) wstdcall;
	void (*enable_interrupt)(void *ctx) wstdcall;
	void (*mp_halt)(void *ctx) wstdcall;
	ndis_interrupt_handler handle_interrupt;
	NDIS_STATUS (*init)(NDIS_STATUS *error_status, UINT *medium_index,
			    enum ndis_medium medium[], UINT medium_array_size,
			    void *handle, void *conf_handle) wstdcall;
	ndis_isr_handler isr;
	NDIS_STATUS (*queryinfo)(void *ctx, ndis_oid oid, void *buffer,
			     ULONG buflen, ULONG *written,
			     ULONG *needed) wstdcall;
	void *reconfig;
	NDIS_STATUS (*reset)(BOOLEAN *reset_address, void *ctx) wstdcall;
	NDIS_STATUS (*send)(void *ctx, struct ndis_packet *packet,
			    UINT flags) wstdcall;
	NDIS_STATUS (*setinfo)(void *ctx, ndis_oid oid, void *buffer,
			       ULONG buflen, ULONG *written,
			       ULONG *needed) wstdcall;
	NDIS_STATUS (*tx_data)(struct ndis_packet *ndis_packet,
			       UINT *bytes_txed, void *mp_ctx, void *rx_ctx,
			       UINT offset, UINT bytes_to_tx) wstdcall;
	/* NDIS 4.0 extensions */
	void (*return_packet)(void *ctx, void *packet) wstdcall;
	void (*send_packets)(void *ctx, struct ndis_packet **packets,
			     INT nr_of_packets) wstdcall;
	void (*alloc_complete)(void *handle, void *virt,
			       NDIS_PHY_ADDRESS *phys,
			       ULONG size, void *ctx) wstdcall;
	/* NDIS 5.0 extensions */
	NDIS_STATUS (*co_create_vc)(void *ctx, void *vc_handle,
				    void *vc_ctx) wstdcall;
	NDIS_STATUS (*co_delete_vc)(void *vc_ctx) wstdcall;
	NDIS_STATUS (*co_activate_vc)(void *vc_ctx, void *call_params) wstdcall;
	NDIS_STATUS (*co_deactivate_vc)(void *vc_ctx) wstdcall;
	NDIS_STATUS (*co_send_packets)(void *vc_ctx, void **packets,
				       UINT nr_of_packets) wstdcall;
	NDIS_STATUS (*co_request)(void *ctx, void *vc_ctx, UINT *req) wstdcall;
	/* NDIS 5.1 extensions */
	void (*cancel_send_packets)(void *ctx, void *id) wstdcall;
	void (*pnp_event_notify)(void *ctx, enum ndis_device_pnp_event event,
				 void *inf_buf, ULONG inf_buf_len) wstdcall;
	void (*shutdown)(void *ctx) wstdcall;
	void *reserved1;
	void *reserved2;
	void *reserved3;
	void *reserved4;
};

struct ndis_spinlock {
	NT_SPIN_LOCK klock;
	KIRQL irql;
};

union ndis_rw_lock_refcount {
	UCHAR cache_line[16];
};

struct ndis_rw_lock {
	union {
		struct {
			NT_SPIN_LOCK klock;
			void *context;
		};
		UCHAR reserved[16];
	};
	union {
		union ndis_rw_lock_refcount ref_count[MAXIMUM_PROCESSORS];
		/* ndiswrapper specific */
		volatile int count;
	};
};

struct lock_state {
	USHORT state;
	KIRQL irql;
};

struct ndis_work_item;
typedef void (*NDIS_PROC)(struct ndis_work_item *, void *) wstdcall;

struct ndis_work_item {
	void *ctx;
	NDIS_PROC func;
	union {
		UCHAR reserved[8 * sizeof(void *)];
		/* ndiswrapper specific */
		struct nt_list list;
	};
};

struct alloc_shared_mem {
	void *ctx;
	ULONG size;
	BOOLEAN cached;
};

struct ndis_mp_block;

/* this is opaque to drivers, so we can use it as we please */
struct ndis_mp_interrupt {
	struct kinterrupt *kinterrupt;
	NT_SPIN_LOCK lock;
	union {
		void *reserved;
		unsigned int irq;
	};
	ndis_isr_handler isr;
	ndis_interrupt_handler mp_dpc;
	struct kdpc intr_dpc;
	struct ndis_mp_block *nmb;
	UCHAR dpc_count;
	BOOLEAN enable;
	struct nt_event dpc_completed_event;
	BOOLEAN shared;
	BOOLEAN req_isr;
};

struct ndis_binary_data {
	USHORT len;
	void *buf;
};

enum ndis_parameter_type {
	NdisParameterInteger, NdisParameterHexInteger,
	NdisParameterString, NdisParameterMultiString, NdisParameterBinary,
};

typedef struct unicode_string NDIS_STRING;

struct ndis_configuration_parameter {
	enum ndis_parameter_type type;
	union {
		ULONG integer;
		NDIS_STRING string;
	} data;
};

struct ndis_driver {
	struct miniport mp;
};

/* IDs used to store extensions in driver_object's custom extension */
#define NDIS_DRIVER_CLIENT_ID 10

struct ndis_wireless_stats {
	ULONG length;
	LARGE_INTEGER tx_frag;
	LARGE_INTEGER tx_multi_frag;
	LARGE_INTEGER failed;
	LARGE_INTEGER retry;
	LARGE_INTEGER multi_retry;
	LARGE_INTEGER rtss_succ;
	LARGE_INTEGER rtss_fail;
	LARGE_INTEGER ack_fail;
	LARGE_INTEGER frame_dup;
	LARGE_INTEGER rx_frag;
	LARGE_INTEGER rx_multi_frag;
	LARGE_INTEGER fcs_err;
	LARGE_INTEGER tkip_local_mic_failures;
	LARGE_INTEGER tkip_icv_errors;
	LARGE_INTEGER tkip_counter_measures_invoked;
	LARGE_INTEGER tkip_replays;
	LARGE_INTEGER ccmp_format_errors;
	LARGE_INTEGER ccmp_replays;
	LARGE_INTEGER ccmp_decrypt_errors;
	LARGE_INTEGER fourway_handshake_failures;
	LARGE_INTEGER wep_undecryptable_count;
	LARGE_INTEGER wep_icv_errorcount;
	LARGE_INTEGER decrypt_success_count;
	LARGE_INTEGER decrypt_failure_count;
};

enum ndis_status_type {
	Ndis802_11StatusType_Authentication,
	Ndis802_11StatusType_MediaStreamMode,
	Ndis802_11StatusType_PMKID_CandidateList,
	Ndis802_11StatusType_RadioState,
};

struct ndis_status_indication {
	enum ndis_status_type status_type;
};

enum ndis_radio_status {
	Ndis802_11RadioStatusOn, Ndis802_11RadioStatusHardwareOff,
	Ndis802_11RadioStatusSoftwareOff,
};

struct ndis_radio_status_indication
{
	enum ndis_status_type status_type;
	enum ndis_radio_status radio_state;
};

enum ndis_media_state {
	NdisMediaStateConnected,
	NdisMediaStateDisconnected,
};

enum ndis_media_stream_mode {
	Ndis802_11MediaStreamOff, Ndis802_11MediaStreamOn
};

enum wrapper_work {
	LINK_STATUS_OFF, LINK_STATUS_ON, SET_MULTICAST_LIST, COLLECT_IW_STATS,
	HANGCHECK, NETIF_WAKEQ,
};

struct encr_info {
	struct encr_key {
		ULONG length;
		UCHAR key[NDIS_ENCODING_TOKEN_MAX];
	} keys[MAX_ENCR_KEYS];
	unsigned short tx_key_index;
};

struct ndis_essid {
	ULONG length;
	UCHAR essid[NDIS_ESSID_MAX_SIZE];
};

enum ndis_infrastructure_mode {
	Ndis802_11IBSS, Ndis802_11Infrastructure, Ndis802_11AutoUnknown,
	Ndis802_11InfrastructureMax
};

enum authentication_mode {
	Ndis802_11AuthModeOpen, Ndis802_11AuthModeShared,
	Ndis802_11AuthModeAutoSwitch, Ndis802_11AuthModeWPA,
	Ndis802_11AuthModeWPAPSK, Ndis802_11AuthModeWPANone,
	Ndis802_11AuthModeWPA2, Ndis802_11AuthModeWPA2PSK,
	Ndis802_11AuthModeMax
};

enum encryption_status {
	Ndis802_11WEPEnabled,
	Ndis802_11Encryption1Enabled = Ndis802_11WEPEnabled,
	Ndis802_11WEPDisabled,
	Ndis802_11EncryptionDisabled = Ndis802_11WEPDisabled,
	Ndis802_11WEPKeyAbsent,
	Ndis802_11Encryption1KeyAbsent = Ndis802_11WEPKeyAbsent,
	Ndis802_11WEPNotSupported,
	Ndis802_11EncryptionNotSupported = Ndis802_11WEPNotSupported,
	Ndis802_11Encryption2Enabled, Ndis802_11Encryption2KeyAbsent,
	Ndis802_11Encryption3Enabled, Ndis802_11Encryption3KeyAbsent
};

struct ndis_auth_encr_pair {
	enum authentication_mode auth_mode;
	enum encryption_status encr_mode;
};

struct ndis_capability {
	ULONG length;
	ULONG version;
	ULONG num_PMKIDs;
	ULONG num_auth_encr_pair;
	struct ndis_auth_encr_pair auth_encr_pair[1];
};

struct ndis_guid {
	struct guid guid;
	union {
		ndis_oid oid;
		NDIS_STATUS status;
	};
	ULONG size;
	ULONG flags;
};

struct ndis_timer {
	struct nt_timer nt_timer;
	struct kdpc kdpc;
};

struct ndis_mp_timer {
	struct nt_timer nt_timer;
	struct kdpc kdpc;
	DPC func;
	void *ctx;
	struct ndis_mp_block *nmb;
	struct ndis_mp_timer *next;
};

typedef struct cm_partial_resource_list NDIS_RESOURCE_LIST;

struct ndis_event {
	struct nt_event nt_event;
};

struct ndis_bind_paths {
	UINT number;
	struct unicode_string paths[1];
};

struct ndis_reference {
	NT_SPIN_LOCK lock;
	USHORT ref_count;
	BOOLEAN closing;
};

struct ndis_filterdbs {
	union {
		void *eth_db;
		void *null_db;
	};
	void *tr_db;
	void *fddi_db;
	void *arc_db;
};

enum ndis_interface_type {
	NdisInterfaceInternal, NdisInterfaceIsa, NdisInterfaceEisa,
	NdisInterfaceMca, NdisInterfaceTurboChannel, NdisInterfacePci,
	NdisInterfacePcMcia,
};

struct auth_encr_capa {
	unsigned long auth;
	unsigned long encr;
};

struct ndis_pmkid_candidate {
	mac_address bssid;
	DWORD flags;
};

struct ndis_pmkid_candidate_list {
	ULONG version;
	ULONG num_candidates;
	struct ndis_pmkid_candidate candidates[1];
};

/*
 * This struct contains function pointers that the drivers references
 * directly via macros, so it's important that they are at the correct
 * position.
 */
struct ndis_mp_block {
	void *signature;
	struct ndis_mp_block *next;
	struct driver_object *drv_obj;
	void *mp_ctx;
	struct unicode_string name;
	struct ndis_bind_paths *bindpaths;
	void *openqueue;
	struct ndis_reference reference;
	void *device_ctx;
	UCHAR padding;
	UCHAR lock_acquired;
	UCHAR pmode_opens;
	UCHAR assigned_cpu;
	NT_SPIN_LOCK lock;
	enum ndis_request_type *mediarequest;
	struct ndis_mp_interrupt *interrupt;
	ULONG flags;
	ULONG pnp_flags;
	struct nt_list packet_list;
	struct ndis_packet *first_pending_tx_packet;
	struct ndis_packet *return_packet_queue;
	ULONG request_buffer;
	void *set_mcast_buffer;
	struct ndis_mp_block *primary_mp;
	void *wrapper_ctx;
	void *bus_data_ctx;
	ULONG pnp_capa;
	void *resources;
	struct ndis_timer wakeup_dpc_timer;
	struct unicode_string basename;
	struct unicode_string symlink_name;
	ULONG ndis_hangcheck_interval;
	USHORT hanghcheck_ticks;
	USHORT hangcheck_tick;
	NDIS_STATUS ndis_reset_status;
	void *resetopen;
	struct ndis_filterdbs filterdbs;
	void *rx_packet;
	void *send_complete;
	void *send_resource_avail;
	void *reset_complete;

	enum ndis_medium media_type;
	ULONG bus_number;
	enum ndis_interface_type bus_type;
	enum ndis_interface_type adapter_type;
	struct device_object *fdo;
	struct device_object *pdo;
	struct device_object *next_device;
	void *mapreg;
	void *call_mgraflist;
	void *mp_thread;
	void *setinfobuf;
	USHORT setinfo_buf_len;
	USHORT max_send_pkts;
	NDIS_STATUS fake_status;
	void *lock_handler;
	struct unicode_string *adapter_instance_name;
	void *timer_queue;
	UINT mac_options;
	void *pending_req;
	UINT max_long_addrs;
	UINT max_short_addrs;
	UINT cur_lookahead;
	UINT max_lookahead;

	ndis_interrupt_handler irq_bh;
	void *disable_intr;
	void *enable_intr;
	void *send_pkts;
	void *deferred_send;
	void *eth_rx_indicate;
	void *tr_rx_indicate;
	void *fddi_rx_indicate;
	void *eth_rx_complete;
	void *tr_rx_complete;
	void *fddi_rx_complete;

	void *status;
	void *status_complete;
	void *td_complete;

	void *queryinfo_complete;
	void *setinfo_complete;
	void *wan_tx_complete;
	void *wan_rx;
	void *wan_rx_complete;
	/* ndiswrapper specific */
	struct ndis_device *wnd;
};

struct ndis_device {
	struct ndis_mp_block *nmb;
	struct wrap_device *wd;
	struct net_device *net_dev;
	void *shutdown_ctx;
	struct ndis_mp_interrupt *mp_interrupt;
	struct kdpc irq_kdpc;
	unsigned long mem_start;
	unsigned long mem_end;

	struct net_device_stats net_stats;
	struct iw_statistics iw_stats;
	BOOLEAN iw_stats_enabled;
	struct ndis_wireless_stats ndis_stats;

	work_struct_t tx_work;
	struct ndis_packet *tx_ring[TX_RING_SIZE];
	u8 tx_ring_start;
	u8 tx_ring_end;
	u8 is_tx_ring_full;
	u8 tx_ok;
	spinlock_t tx_ring_lock;
	struct semaphore tx_ring_mutex;
	unsigned int max_tx_packets;
	struct semaphore ndis_req_mutex;
	struct task_struct *ndis_req_task;
	int ndis_req_done;
	NDIS_STATUS ndis_req_status;
	ULONG packet_filter;

	ULONG sg_dma_size;
	ULONG dma_map_count;
	dma_addr_t *dma_map_addr;

	int hangcheck_interval;
	struct timer_list hangcheck_timer;
	int iw_stats_interval;
	struct timer_list iw_stats_timer;
	unsigned long scan_timestamp;
	struct encr_info encr_info;
	char nick[IW_ESSID_MAX_SIZE + 1];
	struct ndis_essid essid;
	struct auth_encr_capa capa;
	enum ndis_infrastructure_mode infrastructure_mode;
	int max_pmkids;
	int num_pmkids;
	struct ndis_pmkid *pmkids;
	mac_address mac;
	struct proc_dir_entry *procfs_iface;

	work_struct_t ndis_work;
	unsigned long ndis_pending_work;
	UINT attributes;
	int iw_auth_wpa_version;
	int iw_auth_cipher_pairwise;
	int iw_auth_cipher_group;
	int iw_auth_key_mgmt;
	int iw_auth_80211_alg;
	struct ndis_packet_pool *tx_packet_pool;
	struct ndis_buffer_pool *tx_buffer_pool;
	int multicast_size;
	struct v4_checksum rx_csum;
	struct v4_checksum tx_csum;
	enum ndis_physical_medium physical_medium;
	ULONG ndis_wolopts;
	struct nt_slist wrap_timer_slist;
	int drv_ndis_version;
	struct ndis_pnp_capabilities pnp_capa;
	char netdev_name[IFNAMSIZ];
};

BOOLEAN ndis_isr(struct kinterrupt *kinterrupt, void *ctx) wstdcall;

int ndis_init(void);
void ndis_exit(void);
int ndis_init_device(struct ndis_device *wnd);
void ndis_exit_device(struct ndis_device *wnd);

int wrap_procfs_add_ndis_device(struct ndis_device *wnd);
void wrap_procfs_remove_ndis_device(struct ndis_device *wnd);

void NdisAllocatePacketPoolEx(NDIS_STATUS *status,
			      struct ndis_packet_pool **pool_handle,
			      UINT num_descr, UINT overflowsize,
			      UINT proto_rsvd_length) wstdcall;
void NdisFreePacketPool(struct ndis_packet_pool *pool) wstdcall;
void NdisAllocatePacket(NDIS_STATUS *status, struct ndis_packet **packet,
			struct ndis_packet_pool *pool) wstdcall;
void NdisFreePacket(struct ndis_packet *descr) wstdcall;
void NdisAllocateBufferPool(NDIS_STATUS *status,
			    struct ndis_buffer_pool **pool_handle,
			    UINT num_descr) wstdcall;
void NdisFreeBufferPool(struct ndis_buffer_pool *pool) wstdcall;
void NdisAllocateBuffer(NDIS_STATUS *status, ndis_buffer **buffer,
			struct ndis_buffer_pool *pool, void *virt,
			UINT length) wstdcall;
void NdisFreeBuffer(ndis_buffer *descr) wstdcall;
void NdisMIndicateReceivePacket(struct ndis_mp_block *nmb,
				struct ndis_packet **packets,
				UINT nr_packets) wstdcall;
void NdisMSendComplete(struct ndis_mp_block *nmb, struct ndis_packet *packet,
		       NDIS_STATUS status) wstdcall;
void NdisMSendResourcesAvailable(struct ndis_mp_block *nmb) wstdcall;
void NdisMIndicateStatus(struct ndis_mp_block *nmb,
			 NDIS_STATUS status, void *buf, UINT len) wstdcall;
void NdisMIndicateStatusComplete(struct ndis_mp_block *nmb) wstdcall;
void NdisMQueryInformationComplete(struct ndis_mp_block *nmb,
				   NDIS_STATUS status) wstdcall;
void NdisMSetInformationComplete(struct ndis_mp_block *nmb,
				 NDIS_STATUS status) wstdcall;
void NdisMResetComplete(struct ndis_mp_block *nmb, NDIS_STATUS status,
			BOOLEAN address_reset) wstdcall;
ULONG NDIS_BUFFER_TO_SPAN_PAGES(ndis_buffer *buffer) wstdcall;
BOOLEAN NdisWaitEvent(struct ndis_event *event, UINT timeout) wstdcall;
void NdisSetEvent(struct ndis_event *event) wstdcall;
void NdisMDeregisterInterrupt(struct ndis_mp_interrupt *mp_interrupt) wstdcall;
void EthRxIndicateHandler(struct ndis_mp_block *nmb, void *rx_ctx,
			  char *header1, char *header, UINT header_size,
			  void *look_ahead, UINT look_ahead_size,
			  UINT packet_size) wstdcall;
void EthRxComplete(struct ndis_mp_block *nmb) wstdcall;
void NdisMTransferDataComplete(struct ndis_mp_block *nmb,
			       struct ndis_packet *packet, NDIS_STATUS status,
			       UINT bytes_txed) wstdcall;
void NdisWriteConfiguration(NDIS_STATUS *status, struct ndis_mp_block *nmb,
			    struct unicode_string *key,
			    struct ndis_configuration_parameter *param) wstdcall;
void NdisReadConfiguration(NDIS_STATUS *status,
			   struct ndis_configuration_parameter **param,
			   struct ndis_mp_block *nmb,
			   struct unicode_string *key,
			   enum ndis_parameter_type type) wstdcall;

/* Required OIDs */
#define OID_GEN_SUPPORTED_LIST			0x00010101
#define OID_GEN_HARDWARE_STATUS			0x00010102
#define OID_GEN_MEDIA_SUPPORTED			0x00010103
#define OID_GEN_MEDIA_IN_USE			0x00010104
#define OID_GEN_MAXIMUM_LOOKAHEAD		0x00010105
#define OID_GEN_MAXIMUM_FRAME_SIZE		0x00010106
#define OID_GEN_LINK_SPEED			0x00010107
#define OID_GEN_TRANSMIT_BUFFER_SPACE		0x00010108
#define OID_GEN_RECEIVE_BUFFER_SPACE		0x00010109
#define OID_GEN_TRANSMIT_BLOCK_SIZE		0x0001010A
#define OID_GEN_RECEIVE_BLOCK_SIZE		0x0001010B
#define OID_GEN_VENDOR_ID			0x0001010C
#define OID_GEN_VENDOR_DESCRIPTION		0x0001010D
#define OID_GEN_CURRENT_PACKET_FILTER		0x0001010E
#define OID_GEN_CURRENT_LOOKAHEAD		0x0001010F
#define OID_GEN_DRIVER_VERSION			0x00010110
#define OID_GEN_MAXIMUM_TOTAL_SIZE		0x00010111
#define OID_GEN_PROTOCOL_OPTIONS		0x00010112
#define OID_GEN_MAC_OPTIONS			0x00010113
#define OID_GEN_MEDIA_CONNECT_STATUS		0x00010114
#define OID_GEN_MAXIMUM_SEND_PACKETS		0x00010115
#define OID_GEN_VENDOR_DRIVER_VERSION		0x00010116
#define OID_GEN_SUPPORTED_GUIDS			0x00010117
#define OID_GEN_NETWORK_LAYER_ADDRESSES		0x00010118	/* Set only */
#define OID_GEN_TRANSPORT_HEADER_OFFSET		0x00010119	/* Set only */
#define OID_GEN_MACHINE_NAME			0x0001021A
#define OID_GEN_RNDIS_CONFIG_PARAMETER		0x0001021B	/* Set only */
#define OID_GEN_VLAN_ID				0x0001021C

/* Optional OIDs. */
#define OID_GEN_MEDIA_CAPABILITIES		0x00010201
#define OID_GEN_PHYSICAL_MEDIUM			0x00010202

/* Required statistics OIDs. */
#define OID_GEN_XMIT_OK				0x00020101
#define OID_GEN_RCV_OK				0x00020102
#define OID_GEN_XMIT_ERROR			0x00020103
#define OID_GEN_RCV_ERROR			0x00020104
#define OID_GEN_RCV_NO_BUFFER			0x00020105

/* Optional OID statistics */
#define OID_GEN_DIRECTED_BYTES_XMIT		0x00020201
#define OID_GEN_DIRECTED_FRAMES_XMIT		0x00020202
#define OID_GEN_MULTICAST_BYTES_XMIT		0x00020203
#define OID_GEN_MULTICAST_FRAMES_XMIT		0x00020204
#define OID_GEN_BROADCAST_BYTES_XMIT		0x00020205
#define OID_GEN_BROADCAST_FRAMES_XMIT		0x00020206
#define OID_GEN_DIRECTED_BYTES_RCV		0x00020207
#define OID_GEN_DIRECTED_FRAMES_RCV		0x00020208
#define OID_GEN_MULTICAST_BYTES_RCV		0x00020209
#define OID_GEN_MULTICAST_FRAMES_RCV		0x0002020A
#define OID_GEN_BROADCAST_BYTES_RCV		0x0002020B
#define OID_GEN_BROADCAST_FRAMES_RCV		0x0002020C
#define OID_GEN_RCV_CRC_ERROR			0x0002020D
#define OID_GEN_TRANSMIT_QUEUE_LENGTH		0x0002020E
#define OID_GEN_GET_TIME_CAPS			0x0002020F
#define OID_GEN_GET_NETCARD_TIME		0x00020210
#define OID_GEN_NETCARD_LOAD			0x00020211
#define OID_GEN_DEVICE_PROFILE			0x00020212

/* 802.3 (ethernet) OIDs */
#define OID_802_3_PERMANENT_ADDRESS		0x01010101
#define OID_802_3_CURRENT_ADDRESS		0x01010102
#define OID_802_3_MULTICAST_LIST		0x01010103
#define OID_802_3_MAXIMUM_LIST_SIZE		0x01010104
#define OID_802_3_MAC_OPTIONS			0x01010105
#define NDIS_802_3_MAC_OPTION_PRIORITY		0x00000001
#define OID_802_3_RCV_ERROR_ALIGNMENT		0x01020101
#define OID_802_3_XMIT_ONE_COLLISION		0x01020102
#define OID_802_3_XMIT_MORE_COLLISIONS		0x01020103
#define OID_802_3_XMIT_DEFERRED			0x01020201
#define OID_802_3_XMIT_MAX_COLLISIONS		0x01020202
#define OID_802_3_RCV_OVERRUN			0x01020203
#define OID_802_3_XMIT_UNDERRUN			0x01020204
#define OID_802_3_XMIT_HEARTBEAT_FAILURE	0x01020205
#define OID_802_3_XMIT_TIMES_CRS_LOST		0x01020206
#define OID_802_3_XMIT_LATE_COLLISIONS		0x01020207

/* PnP and power management OIDs */
#define OID_PNP_CAPABILITIES			0xFD010100
#define OID_PNP_SET_POWER			0xFD010101
#define OID_PNP_QUERY_POWER			0xFD010102
#define OID_PNP_ADD_WAKE_UP_PATTERN		0xFD010103
#define OID_PNP_REMOVE_WAKE_UP_PATTERN		0xFD010104
#define OID_PNP_WAKE_UP_PATTERN_LIST		0xFD010105
#define OID_PNP_ENABLE_WAKE_UP			0xFD010106

/* PnP/PM Statistics (Optional). */
#define OID_PNP_WAKE_UP_OK			0xFD020200
#define OID_PNP_WAKE_UP_ERROR			0xFD020201

/* The following bits are defined for OID_PNP_ENABLE_WAKE_UP */
#define NDIS_PNP_WAKE_UP_MAGIC_PACKET		0x00000001
#define NDIS_PNP_WAKE_UP_PATTERN_MATCH		0x00000002
#define NDIS_PNP_WAKE_UP_LINK_CHANGE		0x00000004

/* 802.11 OIDs */
#define OID_802_11_BSSID			0x0D010101
#define OID_802_11_SSID				0x0D010102
#define OID_802_11_NETWORK_TYPES_SUPPORTED	0x0D010203
#define OID_802_11_NETWORK_TYPE_IN_USE		0x0D010204
#define OID_802_11_TX_POWER_LEVEL		0x0D010205
#define OID_802_11_RSSI				0x0D010206
#define OID_802_11_RSSI_TRIGGER			0x0D010207
#define OID_802_11_INFRASTRUCTURE_MODE		0x0D010108
#define OID_802_11_FRAGMENTATION_THRESHOLD	0x0D010209
#define OID_802_11_RTS_THRESHOLD		0x0D01020A
#define OID_802_11_NUMBER_OF_ANTENNAS		0x0D01020B
#define OID_802_11_RX_ANTENNA_SELECTED		0x0D01020C
#define OID_802_11_TX_ANTENNA_SELECTED		0x0D01020D
#define OID_802_11_SUPPORTED_RATES		0x0D01020E
#define OID_802_11_DESIRED_RATES		0x0D010210
#define OID_802_11_CONFIGURATION		0x0D010211
#define OID_802_11_STATISTICS			0x0D020212
#define OID_802_11_ADD_WEP			0x0D010113
#define OID_802_11_REMOVE_WEP			0x0D010114
#define OID_802_11_DISASSOCIATE			0x0D010115
#define OID_802_11_POWER_MODE			0x0D010216
#define OID_802_11_BSSID_LIST			0x0D010217
#define OID_802_11_AUTHENTICATION_MODE		0x0D010118
#define OID_802_11_PRIVACY_FILTER		0x0D010119
#define OID_802_11_BSSID_LIST_SCAN		0x0D01011A
#define OID_802_11_WEP_STATUS			0x0D01011B
#define OID_802_11_ENCRYPTION_STATUS		OID_802_11_WEP_STATUS
#define OID_802_11_RELOAD_DEFAULTS		0x0D01011C
#define OID_802_11_ADD_KEY			0x0D01011D
#define OID_802_11_REMOVE_KEY			0x0D01011E
#define OID_802_11_ASSOCIATION_INFORMATION	0x0D01011F
#define OID_802_11_TEST				0x0D010120
#define OID_802_11_MEDIA_STREAM_MODE		0x0D010121
#define OID_802_11_CAPABILITY			0x0D010122
#define OID_802_11_PMKID			0x0D010123

#define NDIS_STATUS_SUCCESS		0
#define NDIS_STATUS_PENDING		0x00000103
#define NDIS_STATUS_NOT_RECOGNIZED	0x00010001
#define NDIS_STATUS_NOT_COPIED		0x00010002
#define NDIS_STATUS_NOT_ACCEPTED	0x00010003
#define NDIS_STATUS_CALL_ACTIVE		0x00010007
#define NDIS_STATUS_ONLINE		0x40010003
#define NDIS_STATUS_RESET_START		0x40010004
#define NDIS_STATUS_RESET_END		0x40010005
#define NDIS_STATUS_RING_STATUS		0x40010006
#define NDIS_STATUS_CLOSED		0x40010007
#define NDIS_STATUS_WAN_LINE_UP		0x40010008
#define NDIS_STATUS_WAN_LINE_DOWN	0x40010009
#define NDIS_STATUS_WAN_FRAGMENT	0x4001000A
#define NDIS_STATUS_MEDIA_CONNECT	0x4001000B
#define NDIS_STATUS_MEDIA_DISCONNECT	0x4001000C
#define NDIS_STATUS_HARDWARE_LINE_UP	0x4001000D
#define NDIS_STATUS_HARDWARE_LINE_DOWN	0x4001000E
#define NDIS_STATUS_INTERFACE_UP	0x4001000F
#define NDIS_STATUS_INTERFACE_DOWN	0x40010010
#define NDIS_STATUS_MEDIA_BUSY		0x40010011
#define NDIS_STATUS_MEDIA_SPECIFIC_INDICATION	0x40010012
#define NDIS_STATUS_WW_INDICATION NDIS_STATUS_MEDIA_SPECIFIC_INDICATION
#define NDIS_STATUS_LINK_SPEED_CHANGE	0x40010013
#define NDIS_STATUS_WAN_GET_STATS	0x40010014
#define NDIS_STATUS_WAN_CO_FRAGMENT	0x40010015
#define NDIS_STATUS_WAN_CO_LINKPARAMS	0x40010016
#define NDIS_STATUS_NOT_RESETTABLE	0x80010001
#define NDIS_STATUS_SOFT_ERRORS		0x80010003
#define NDIS_STATUS_HARD_ERRORS		0x80010004
#define NDIS_STATUS_BUFFER_OVERFLOW	0x80000005
#define NDIS_STATUS_FAILURE		0xC0000001
#define NDIS_STATUS_INVALID_PARAMETER 0xC000000D
#define NDIS_STATUS_RESOURCES		0xC000009A
#define NDIS_STATUS_CLOSING		0xC0010002
#define NDIS_STATUS_BAD_VERSION		0xC0010004
#define NDIS_STATUS_BAD_CHARACTERISTICS	0xC0010005
#define NDIS_STATUS_ADAPTER_NOT_FOUND	0xC0010006
#define NDIS_STATUS_OPEN_FAILED		0xC0010007
#define NDIS_STATUS_DEVICE_FAILED	0xC0010008
#define NDIS_STATUS_MULTICAST_FULL	0xC0010009
#define NDIS_STATUS_MULTICAST_EXISTS	0xC001000A
#define NDIS_STATUS_MULTICAST_NOT_FOUND	0xC001000B
#define NDIS_STATUS_REQUEST_ABORTED	0xC001000C
#define NDIS_STATUS_RESET_IN_PROGRESS	0xC001000D
#define NDIS_STATUS_CLOSING_INDICATING	0xC001000E
#define NDIS_STATUS_BAD_VERSION		0xC0010004
#define NDIS_STATUS_NOT_SUPPORTED	0xC00000BB
#define NDIS_STATUS_INVALID_PACKET	0xC001000F
#define NDIS_STATUS_OPEN_LIST_FULL	0xC0010010
#define NDIS_STATUS_ADAPTER_NOT_READY	0xC0010011
#define NDIS_STATUS_ADAPTER_NOT_OPEN	0xC0010012
#define NDIS_STATUS_NOT_INDICATING	0xC0010013
#define NDIS_STATUS_INVALID_LENGTH	0xC0010014
#define NDIS_STATUS_INVALID_DATA	0xC0010015
#define NDIS_STATUS_BUFFER_TOO_SHORT	0xC0010016
#define NDIS_STATUS_INVALID_OID		0xC0010017
#define NDIS_STATUS_ADAPTER_REMOVED	0xC0010018
#define NDIS_STATUS_UNSUPPORTED_MEDIA	0xC0010019
#define NDIS_STATUS_GROUP_ADDRESS_IN_USE	0xC001001A
#define NDIS_STATUS_FILE_NOT_FOUND	0xC001001B
#define NDIS_STATUS_ERROR_READING_FILE	0xC001001C
#define NDIS_STATUS_ALREADY_MAPPED	0xC001001D
#define NDIS_STATUS_RESOURCE_CONFLICT	0xC001001E
#define NDIS_STATUS_NO_CABLE		0xC001001F
#define NDIS_STATUS_INVALID_SAP		0xC0010020
#define NDIS_STATUS_SAP_IN_USE		0xC0010021
#define NDIS_STATUS_INVALID_ADDRESS	0xC0010022
#define NDIS_STATUS_VC_NOT_ACTIVATED	0xC0010023
#define NDIS_STATUS_DEST_OUT_OF_ORDER	0xC0010024
#define NDIS_STATUS_VC_NOT_AVAILABLE	0xC0010025
#define NDIS_STATUS_CELLRATE_NOT_AVAILABLE	0xC0010026
#define NDIS_STATUS_INCOMPATABLE_QOS	0xC0010027
#define NDIS_STATUS_AAL_PARAMS_UNSUPPORTED	0xC0010028
#define NDIS_STATUS_NO_ROUTE_TO_DESTINATION	0xC0010029
#define NDIS_STATUS_TOKEN_RING_OPEN_ERROR	0xC0011000
#define NDIS_STATUS_INVALID_DEVICE_REQUEST	0xC0000010
#define NDIS_STATUS_NETWORK_UNREACHABLE         0xC000023C

/* Event codes */

#define EVENT_NDIS_RESOURCE_CONFLICT	0xC0001388
#define EVENT_NDIS_OUT_OF_RESOURCE	0xC0001389
#define EVENT_NDIS_HARDWARE_FAILURE	0xC000138A
#define EVENT_NDIS_ADAPTER_NOT_FOUND	0xC000138B
#define EVENT_NDIS_INTERRUPT_CONNECT	0xC000138C
#define EVENT_NDIS_DRIVER_FAILURE	0xC000138D
#define EVENT_NDIS_BAD_VERSION		0xC000138E
#define EVENT_NDIS_TIMEOUT		0x8000138F
#define EVENT_NDIS_NETWORK_ADDRESS	0xC0001390
#define EVENT_NDIS_UNSUPPORTED_CONFIGURATION	0xC0001391
#define EVENT_NDIS_INVALID_VALUE_FROM_ADAPTER	0xC0001392
#define EVENT_NDIS_MISSING_CONFIGURATION_PARAMETER	0xC0001393
#define EVENT_NDIS_BAD_IO_BASE_ADDRESS	0xC0001394
#define EVENT_NDIS_RECEIVE_SPACE_SMALL	0x40001395
#define EVENT_NDIS_ADAPTER_DISABLED	0x80001396
#define EVENT_NDIS_IO_PORT_CONFLICT	0x80001397
#define EVENT_NDIS_PORT_OR_DMA_CONFLICT	0x80001398
#define EVENT_NDIS_MEMORY_CONFLICT	0x80001399
#define EVENT_NDIS_INTERRUPT_CONFLICT	0x8000139A
#define EVENT_NDIS_DMA_CONFLICT		0x8000139B
#define EVENT_NDIS_INVALID_DOWNLOAD_FILE_ERROR	0xC000139C
#define EVENT_NDIS_MAXRECEIVES_ERROR	0x8000139D
#define EVENT_NDIS_MAXTRANSMITS_ERROR	0x8000139E
#define EVENT_NDIS_MAXFRAMESIZE_ERROR	0x8000139F
#define EVENT_NDIS_MAXINTERNALBUFS_ERROR	0x800013A0
#define EVENT_NDIS_MAXMULTICAST_ERROR	0x800013A1
#define EVENT_NDIS_PRODUCTID_ERROR	0x800013A2
#define EVENT_NDIS_LOBE_FAILUE_ERROR	0x800013A3
#define EVENT_NDIS_SIGNAL_LOSS_ERROR	0x800013A4
#define EVENT_NDIS_REMOVE_RECEIVED_ERROR	0x800013A5
#define EVENT_NDIS_TOKEN_RING_CORRECTION	0x400013A6
#define EVENT_NDIS_ADAPTER_CHECK_ERROR	0xC00013A7
#define EVENT_NDIS_RESET_FAILURE_ERROR	0x800013A8
#define EVENT_NDIS_CABLE_DISCONNECTED_ERROR	0x800013A9
#define EVENT_NDIS_RESET_FAILURE_CORRECTION	0x800013AA

/* packet filter bits used by NDIS_OID_PACKET_FILTER */
#define NDIS_PACKET_TYPE_DIRECTED               0x00000001
#define NDIS_PACKET_TYPE_MULTICAST              0x00000002
#define NDIS_PACKET_TYPE_ALL_MULTICAST          0x00000004
#define NDIS_PACKET_TYPE_BROADCAST              0x00000008
#define NDIS_PACKET_TYPE_SOURCE_ROUTING         0x00000010
#define NDIS_PACKET_TYPE_PROMISCUOUS            0x00000020
#define NDIS_PACKET_TYPE_SMT                    0x00000040
#define NDIS_PACKET_TYPE_ALL_LOCAL              0x00000080
#define NDIS_PACKET_TYPE_GROUP                  0x00001000
#define NDIS_PACKET_TYPE_ALL_FUNCTIONAL         0x00002000
#define NDIS_PACKET_TYPE_FUNCTIONAL             0x00004000
#define NDIS_PACKET_TYPE_MAC_FRAME              0x00008000

/* memory allocation flags */
#define NDIS_MEMORY_CONTIGUOUS			0x00000001
#define NDIS_MEMORY_NONCACHED			0x00000002

/* Atrribute flags to NdisMSetAtrributesEx */
#define NDIS_ATTRIBUTE_IGNORE_PACKET_TIMEOUT    0x00000001
#define NDIS_ATTRIBUTE_IGNORE_REQUEST_TIMEOUT   0x00000002
#define NDIS_ATTRIBUTE_IGNORE_TOKEN_RING_ERRORS 0x00000004
#define NDIS_ATTRIBUTE_BUS_MASTER               0x00000008
#define NDIS_ATTRIBUTE_INTERMEDIATE_DRIVER      0x00000010
#define NDIS_ATTRIBUTE_DESERIALIZE              0x00000020
#define NDIS_ATTRIBUTE_NO_HALT_ON_SUSPEND       0x00000040
#define NDIS_ATTRIBUTE_SURPRISE_REMOVE_OK       0x00000080
#define NDIS_ATTRIBUTE_NOT_CO_NDIS              0x00000100
#define NDIS_ATTRIBUTE_USES_SAFE_BUFFER_APIS    0x00000200

#define OID_TCP_TASK_OFFLOAD			0xFC010201

#define NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA	0x00000001
#define NDIS_MAC_OPTION_RECEIVE_SERIALIZED	0x00000002
#define NDIS_MAC_OPTION_TRANSFERS_NOT_PEND	0x00000004
#define NDIS_MAC_OPTION_NO_LOOPBACK		0x00000008
#define NDIS_MAC_OPTION_FULL_DUPLEX		0x00000010
#define NDIS_MAC_OPTION_EOTX_INDICATION		0x00000020
#define NDIS_MAC_OPTION_8021P_PRIORITY		0x00000040
#define NDIS_MAC_OPTION_SUPPORTS_MAC_ADDRESS_OVERWRITE	0x00000080
#define NDIS_MAC_OPTION_RECEIVE_AT_DPC		0x00000100
#define NDIS_MAC_OPTION_8021Q_VLAN		0x00000200
#define NDIS_MAC_OPTION_RESERVED		0x80000000

#define deserialized_driver(wnd) (wnd->attributes & NDIS_ATTRIBUTE_DESERIALIZE)

static inline void serialize_lock(struct ndis_device *wnd)
{
	nt_spin_lock(&wnd->nmb->lock);
}

static inline void serialize_unlock(struct ndis_device *wnd)
{
	nt_spin_unlock(&wnd->nmb->lock);
}

static inline KIRQL serialize_lock_irql(struct ndis_device *wnd)
{
	if (deserialized_driver(wnd))
		return raise_irql(DISPATCH_LEVEL);
	else
		return nt_spin_lock_irql(&wnd->nmb->lock, DISPATCH_LEVEL);
}

static inline void serialize_unlock_irql(struct ndis_device *wnd,
					 KIRQL irql)
{
	if (deserialized_driver(wnd))
		lower_irql(irql);
	else
		nt_spin_unlock_irql(&wnd->nmb->lock, irql);
}

static inline void if_serialize_lock(struct ndis_device *wnd)
{
	if (!deserialized_driver(wnd))
		nt_spin_lock(&wnd->nmb->lock);
}

static inline void if_serialize_unlock(struct ndis_device *wnd)
{
	if (!deserialized_driver(wnd))
		nt_spin_unlock(&wnd->nmb->lock);
}

#endif /* NDIS_H */
