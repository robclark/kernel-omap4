/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef __INC_QOS_TYPE_H
#define __INC_QOS_TYPE_H

#define BIT0                    0x00000001
#define BIT1                    0x00000002
#define BIT2                    0x00000004
#define BIT3                    0x00000008
#define BIT4                    0x00000010
#define BIT5                    0x00000020
#define BIT6                    0x00000040
#define BIT7                    0x00000080
#define BIT8                    0x00000100
#define BIT9                    0x00000200
#define BIT10                   0x00000400
#define BIT11                   0x00000800
#define BIT12                   0x00001000
#define BIT13                   0x00002000
#define BIT14                   0x00004000
#define BIT15                   0x00008000
#define BIT16                   0x00010000
#define BIT17                   0x00020000
#define BIT18                   0x00040000
#define BIT19                   0x00080000
#define BIT20                   0x00100000
#define BIT21                   0x00200000
#define BIT22                   0x00400000
#define BIT23                   0x00800000
#define BIT24                   0x01000000
#define BIT25                   0x02000000
#define BIT26                   0x04000000
#define BIT27                   0x08000000
#define BIT28                   0x10000000
#define BIT29                   0x20000000
#define BIT30                   0x40000000
#define BIT31                   0x80000000

#define	MAX_WMMELE_LENGTH	64

typedef u32 QOS_MODE, *PQOS_MODE;
#define QOS_DISABLE		0
#define QOS_WMM			1
#define QOS_WMMSA		2
#define QOS_EDCA		4
#define QOS_HCCA		8
#define QOS_WMM_UAPSD		16   

#define AC_PARAM_SIZE	4
#define WMM_PARAM_ELE_BODY_LEN	18

typedef	enum _ACK_POLICY{
	eAckPlc0_ACK		= 0x00,
	eAckPlc1_NoACK		= 0x01,
}ACK_POLICY,*PACK_POLICY;

#define WMM_PARAM_ELEMENT_SIZE	(8+(4*AC_PARAM_SIZE))
#if 0
#define GET_QOS_CTRL(_pStart)	ReadEF2Byte((u8 *)(_pStart) + 24)
#define SET_QOS_CTRL(_pStart, _value)	WriteEF2Byte((u8 *)(_pStart) + 24, _value)

#define GET_QOS_CTRL_WMM_UP(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 0, 3))
#define SET_QOS_CTRL_WMM_UP(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 0, 3, (u8)(_value))

#define GET_QOS_CTRL_WMM_EOSP(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 4, 1))
#define SET_QOS_CTRL_WMM_EOSP(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 4, 1, (u8)(_value))

#define GET_QOS_CTRL_WMM_ACK_POLICY(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 5, 2))
#define SET_QOS_CTRL_WMM_ACK_POLICY(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 5, 2, (u8)(_value))

#define GET_QOS_CTRL_STA_DATA_TID(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 0, 4))
#define SET_QOS_CTRL_STA_DATA_TID(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 0, 4, (u8)(_value))

#define GET_QOS_CTRL_STA_DATA_QSIZE_FLAG(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 4, 1))
#define SET_QOS_CTRL_STA_DATA_QSIZE_FLAG(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 4, 1, (u8)(_value))

#define GET_QOS_CTRL_STA_DATA_ACK_POLICY(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 5, 2))
#define SET_QOS_CTRL_STA_DATA_ACK_POLICY(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 5, 2, (u8)(_value))

#define GET_QOS_CTRL_STA_DATA_TXOP(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 8, 8))
#define SET_QOS_CTRL_STA_DATA_TXOP(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 8, 8, (u8)(_value))

#define GET_QOS_CTRL_STA_DATA_QSIZE(_pStart)	GET_QOS_CTRL_STA_DATA_TXOP(_pStart)
#define SET_QOS_CTRL_STA_DATA_QSIZE(_pStart, _value)	SET_QOS_CTRL_STA_DATA_TXOP(_pStart)

#define GET_QOS_CTRL_HC_DATA_TID(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 0, 4))
#define SET_QOS_CTRL_HC_DATA_TID(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 0, 4, (u8)(_value))

#define GET_QOS_CTRL_HC_DATA_EOSP(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 4, 1))
#define SET_QOS_CTRL_HC_DATA_EOSP(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 4, 1, (u8)(_value))

#define GET_QOS_CTRL_HC_DATA_ACK_POLICY(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 5, 2))
#define SET_QOS_CTRL_HC_DATA_ACK_POLICY(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 5, 2, (u8)(_value))

#define GET_QOS_CTRL_HC_DATA_PS_BUFSTATE(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 8, 8))
#define SET_QOS_CTRL_HC_DATA_PS_BUFSTATE(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 8, 8, (u8)(_value))

#define GET_QOS_CTRL_HC_CFP_TID(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 0, 4))
#define SET_QOS_CTRL_HC_CFP_TID(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 0, 4, (u8)(_value))

#define GET_QOS_CTRL_HC_CFP_EOSP(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 4, 1))
#define SET_QOS_CTRL_HC_CFP_EOSP(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 4, 1, (u8)(_value))

#define GET_QOS_CTRL_HC_CFP_ACK_POLICY(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 5, 2))
#define SET_QOS_CTRL_HC_CFP_ACK_POLICY(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 5, 2, (u8)(_value))

#define GET_QOS_CTRL_HC_CFP_TXOP_LIMIT(_pStart)	((u8)LE_BITS_TO_2BYTE((u8 *)(_pStart)+24, 8, 8))
#define SET_QOS_CTRL_HC_CFP_TXOP_LIMIT(_pStart, _value)	SET_BITS_TO_LE_2BYTE((u8 *)(_pStart)+24, 8, 8, (u8)(_value))

#define SET_WMM_QOS_INFO_FIELD(_pStart, _val)	WriteEF1Byte(_pStart, _val)

#define GET_WMM_QOS_INFO_FIELD_PARAMETERSET_COUNT(_pStart)	LE_BITS_TO_1BYTE(_pStart, 0, 4)
#define SET_WMM_QOS_INFO_FIELD_PARAMETERSET_COUNT(_pStart, _val)	SET_BITS_TO_LE_1BYTE(_pStart, 0, 4, _val)

#define GET_WMM_QOS_INFO_FIELD_AP_UAPSD(_pStart)	LE_BITS_TO_1BYTE(_pStart, 7, 1)
#define SET_WMM_QOS_INFO_FIELD_AP_UAPSD(_pStart, _val)	SET_BITS_TO_LE_1BYTE(_pStart, 7, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_AC_VO_UAPSD(_pStart)	LE_BITS_TO_1BYTE(_pStart, 0, 1)
#define SET_WMM_QOS_INFO_FIELD_STA_AC_VO_UAPSD(_pStart, _val)	SET_BITS_TO_LE_1BYTE(_pStart, 0, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_AC_VI_UAPSD(_pStart)	LE_BITS_TO_1BYTE(_pStart, 1, 1)
#define SET_WMM_QOS_INFO_FIELD_STA_AC_VI_UAPSD(_pStart, _val)	SET_BITS_TO_LE_1BYTE(_pStart, 1, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_AC_BE_UAPSD(_pStart)	LE_BITS_TO_1BYTE(_pStart, 2, 1)
#define SET_WMM_QOS_INFO_FIELD_STA_AC_BE_UAPSD(_pStart, _val)	SET_BITS_TO_LE_1BYTE(_pStart, 2, 1, _val)
	
#define GET_WMM_QOS_INFO_FIELD_STA_AC_BK_UAPSD(_pStart)	LE_BITS_TO_1BYTE(_pStart, 3, 1)
#define SET_WMM_QOS_INFO_FIELD_STA_AC_BK_UAPSD(_pStart, _val)	SET_BITS_TO_LE_1BYTE(_pStart, 3, 1, _val)

#define GET_WMM_QOS_INFO_FIELD_STA_MAX_SP_LEN(_pStart)	LE_BITS_TO_1BYTE(_pStart, 5, 2)
#define SET_WMM_QOS_INFO_FIELD_STA_MAX_SP_LEN(_pStart, _val)	SET_BITS_TO_LE_1BYTE(_pStart, 5, 2, _val)
		

#define WMM_INFO_ELEMENT_SIZE	7

#define GET_WMM_INFO_ELE_OUI(_pStart)	((u8 *)(_pStart))
#define SET_WMM_INFO_ELE_OUI(_pStart, _pVal)	PlatformMoveMemory(_pStart, _pVal, 3);

#define GET_WMM_INFO_ELE_OUI_TYPE(_pStart)	( EF1Byte( *((u8 *)(_pStart)+3) ) )
#define SET_WMM_INFO_ELE_OUI_TYPE(_pStart, _val)	( *((u8 *)(_pStart)+3) = EF1Byte(_val) )

#define GET_WMM_INFO_ELE_OUI_SUBTYPE(_pStart)	( EF1Byte( *((u8 *)(_pStart)+4) ) )
#define SET_WMM_INFO_ELE_OUI_SUBTYPE(_pStart, _val)	( *((u8 *)(_pStart)+4) = EF1Byte(_val) )

#define GET_WMM_INFO_ELE_VERSION(_pStart)	( EF1Byte( *((u8 *)(_pStart)+5) ) )
#define SET_WMM_INFO_ELE_VERSION(_pStart, _val)	( *((u8 *)(_pStart)+5) = EF1Byte(_val) )

#define GET_WMM_INFO_ELE_QOS_INFO_FIELD(_pStart)	( EF1Byte( *((u8 *)(_pStart)+6) ) )
#define SET_WMM_INFO_ELE_QOS_INFO_FIELD(_pStart, _val)	( *((u8 *)(_pStart)+6) = EF1Byte(_val) )

	

#define GET_WMM_AC_PARAM_AIFSN(_pStart)	( (u8)LE_BITS_TO_4BYTE(_pStart, 0, 4) )
#define SET_WMM_AC_PARAM_AIFSN(_pStart, _val)	SET_BITS_TO_LE_4BYTE(_pStart, 0, 4, _val)

#define GET_WMM_AC_PARAM_ACM(_pStart)	( (u8)LE_BITS_TO_4BYTE(_pStart, 4, 1) )
#define SET_WMM_AC_PARAM_ACM(_pStart, _val)	SET_BITS_TO_LE_4BYTE(_pStart, 4, 1, _val)

#define GET_WMM_AC_PARAM_ACI(_pStart)		( (u8)LE_BITS_TO_4BYTE(_pStart, 5, 2) )
#define SET_WMM_AC_PARAM_ACI(_pStart, _val)	SET_BITS_TO_LE_4BYTE(_pStart, 5, 2, _val)

#define GET_WMM_AC_PARAM_ACI_AIFSN(_pStart)	( (u8)LE_BITS_TO_4BYTE(_pStart, 0, 8) )
#define SET_WMM_AC_PARAM_ACI_AIFSN(_pStart, _val)	SET_BTIS_TO_LE_4BYTE(_pStart, 0, 8, _val)

#define GET_WMM_AC_PARAM_ECWMIN(_pStart)	( (u8)LE_BITS_TO_4BYTE(_pStart, 8, 4) )
#define SET_WMM_AC_PARAM_ECWMIN(_pStart, _val)	SET_BITS_TO_LE_4BYTE(_pStart, 8, 4, _val)

#define GET_WMM_AC_PARAM_ECWMAX(_pStart)	( (u8)LE_BITS_TO_4BYTE(_pStart, 12, 4) )
#define SET_WMM_AC_PARAM_ECWMAX(_pStart, _val)	SET_BITS_TO_LE_4BYTE(_pStart, 12, 4, _val)

#define GET_WMM_AC_PARAM_TXOP_LIMIT(_pStart)		( (u16)LE_BITS_TO_4BYTE(_pStart, 16, 16) )
#define SET_WMM_AC_PARAM_TXOP_LIMIT(_pStart, _val)	SET_BITS_TO_LE_4BYTE(_pStart, 16, 16, _val)




#define GET_WMM_PARAM_ELE_OUI(_pStart)	((u8 *)(_pStart))
#define SET_WMM_PARAM_ELE_OUI(_pStart, _pVal)	PlatformMoveMemory(_pStart, _pVal, 3)

#define GET_WMM_PARAM_ELE_OUI_TYPE(_pStart)	( EF1Byte( *((u8 *)(_pStart)+3) ) )
#define SET_WMM_PARAM_ELE_OUI_TYPE(_pStart, _val)	( *((u8 *)(_pStart)+3) = EF1Byte(_val) )

#define GET_WMM_PARAM_ELE_OUI_SUBTYPE(_pStart)	( EF1Byte( *((u8 *)(_pStart)+4) ) )
#define SET_WMM_PARAM_ELE_OUI_SUBTYPE(_pStart, _val)	( *((u8 *)(_pStart)+4) = EF1Byte(_val) )

#define GET_WMM_PARAM_ELE_VERSION(_pStart)	( EF1Byte( *((u8 *)(_pStart)+5) ) )
#define SET_WMM_PARAM_ELE_VERSION(_pStart, _val)	( *((u8 *)(_pStart)+5) = EF1Byte(_val) )

#define GET_WMM_PARAM_ELE_QOS_INFO_FIELD(_pStart)	( EF1Byte( *((u8 *)(_pStart)+6) ) )
#define SET_WMM_PARAM_ELE_QOS_INFO_FIELD(_pStart, _val)	( *((u8 *)(_pStart)+6) = EF1Byte(_val) )

#define GET_WMM_PARAM_ELE_AC_PARAM(_pStart)	( (u8 *)(_pStart)+8 )
#define SET_WMM_PARAM_ELE_AC_PARAM(_pStart, _pVal) PlatformMoveMemory((_pStart)+8, _pVal, 16)
#endif

typedef	union _QOS_CTRL_FIELD{
	u8	charData[2];
	u16	shortData;

	struct
	{
		u8		UP:3;
		u8		usRsvd1:1;
		u8		EOSP:1;
		u8		AckPolicy:2;
		u8		usRsvd2:1;
		u8		ucRsvdByte;
	}WMM;

	struct
	{
		u8		TID:4;
		u8		bIsQsize:1;
		u8		AckPolicy:2;
		u8		usRsvd:1;
		u8		TxopOrQsize;	
	}BySta;

	struct
	{
		u8		TID:4;
		u8		EOSP:1;
		u8		AckPolicy:2;
		u8		usRsvd:1;
		u8		PSBufState;		
	}ByHc_Data;

	struct
	{
		u8		TID:4;
		u8		EOSP:1;
		u8		AckPolicy:2;
		u8		usRsvd:1;
		u8		TxopLimit;		
	}ByHc_CFP;

}QOS_CTRL_FIELD, *PQOS_CTRL_FIELD;


typedef	union _QOS_INFO_FIELD{
	u8	charData;
	
	struct
	{
		u8		ucParameterSetCount:4;
		u8		ucReserved:4;	
	}WMM;

	struct 
	{
		u8		ucAC_VO_UAPSD:1;
		u8		ucAC_VI_UAPSD:1;
		u8		ucAC_BE_UAPSD:1;
		u8		ucAC_BK_UAPSD:1;
		u8		ucReserved1:1;
		u8		ucMaxSPLen:2;
		u8		ucReserved2:1;
		
	}ByWmmPsSta;

	struct
	{
		u8		ucParameterSetCount:4;
		u8		ucReserved:3; 
		u8		ucApUapsd:1;
	}ByWmmPsAp;

	struct
	{
		u8		ucAC3_UAPSD:1;
		u8		ucAC2_UAPSD:1;
		u8		ucAC1_UAPSD:1;
		u8		ucAC0_UAPSD:1;
		u8		ucQAck:1;
		u8		ucMaxSPLen:2;
		u8		ucMoreDataAck:1;
	} By11eSta;

	struct
	{
		u8		ucParameterSetCount:4;
		u8		ucQAck:1;
		u8		ucQueueReq:1;
		u8		ucTXOPReq:1;
		u8		ucReserved:1;
	} By11eAp;

	struct
	{
		u8		ucReserved1:4;
		u8		ucQAck:1;
		u8		ucReserved2:2;
		u8		ucMoreDataAck:1;
	} ByWmmsaSta;

	struct
	{
		u8		ucReserved1:4;
		u8		ucQAck:1;
		u8		ucQueueReq:1;
		u8		ucTXOPReq:1;
		u8		ucReserved2:1;	
	} ByWmmsaAp;

	struct
	{
		u8		ucAC3_UAPSD:1;
		u8		ucAC2_UAPSD:1;
		u8		ucAC1_UAPSD:1;
		u8		ucAC0_UAPSD:1;
		u8		ucQAck:1;
		u8		ucMaxSPLen:2;
		u8		ucMoreDataAck:1;	
	} ByAllSta;

	struct
	{
		u8		ucParameterSetCount:4;
		u8		ucQAck:1;
		u8		ucQueueReq:1;
		u8		ucTXOPReq:1;
		u8		ucApUapsd:1;	
	} ByAllAp;

}QOS_INFO_FIELD, *PQOS_INFO_FIELD;

#if 0
typedef struct _WMM_INFO_ELEMENT{
	u8			OUI[3];
	u8			OUI_Type;
	u8			OUI_SubType;
	u8			Version;
	QOS_INFO_FIELD	QosInfo;
}WMM_INFO_ELEMENT, *PWMM_INFO_ELEMENT;
#endif

typedef u32 AC_CODING;
#define AC0_BE	0		
#define AC1_BK	1		
#define AC2_VI	2		
#define AC3_VO	3		
#define AC_MAX	4		

typedef	union _ACI_AIFSN{
	u8	charData;
	
	struct
	{
		u8	AIFSN:4;
		u8	ACM:1;
		u8	ACI:2;
		u8	Reserved:1;
	}f;	
}ACI_AIFSN, *PACI_AIFSN;

typedef	union _ECW{
	u8	charData;
	struct
	{
		u8	ECWmin:4;
		u8	ECWmax:4;
	}f;	
}ECW, *PECW;

typedef	union _AC_PARAM{
	u32	longData;
	u8	charData[4];

	struct
	{
		ACI_AIFSN	AciAifsn;
		ECW		Ecw;
		u16		TXOPLimit;
	}f;	
}AC_PARAM, *PAC_PARAM;



typedef	enum _QOS_ELE_SUBTYPE{
	QOSELE_TYPE_INFO	= 0x00,		
	QOSELE_TYPE_PARAM	= 0x01,		
}QOS_ELE_SUBTYPE,*PQOS_ELE_SUBTYPE;


typedef	enum _DIRECTION_VALUE{
	DIR_UP			= 0,		
	DIR_DOWN		= 1,		
	DIR_DIRECT		= 2,		
	DIR_BI_DIR		= 3,		
}DIRECTION_VALUE,*PDIRECTION_VALUE;


typedef union _QOS_TSINFO{
	u8		charData[3];
	struct {
		u8		ucTrafficType:1;			
		u8		ucTSID:4;
		u8		ucDirection:2;
		u8		ucAccessPolicy:2;	
		u8		ucAggregation:1;		
		u8		ucPSB:1;				
		u8		ucUP:3;
		u8		ucTSInfoAckPolicy:2;		
		u8		ucSchedule:1;			
		u8		ucReserved:7;
	}field;
}QOS_TSINFO, *PQOS_TSINFO;

typedef union _TSPEC_BODY{
	u8		charData[55];
	
	struct
	{
		QOS_TSINFO	TSInfo;	
		u16	NominalMSDUsize;
		u16	MaxMSDUsize;
		u32	MinServiceItv;
		u32	MaxServiceItv;
		u32	InactivityItv;
		u32	SuspenItv;
		u32	ServiceStartTime;
		u32	MinDataRate;
		u32	MeanDataRate;
		u32	PeakDataRate;
		u32	MaxBurstSize;
		u32	DelayBound;
		u32	MinPhyRate;
		u16	SurplusBandwidthAllowance;
		u16	MediumTime;
	} f;	
}TSPEC_BODY, *PTSPEC_BODY;


typedef struct _WMM_TSPEC{
	u8		ID;
	u8		Length;
	u8		OUI[3];
	u8		OUI_Type;
	u8		OUI_SubType;
	u8		Version;
	TSPEC_BODY	Body;
} WMM_TSPEC, *PWMM_TSPEC;

typedef	enum _ACM_METHOD{
	eAcmWay0_SwAndHw		= 0,		
	eAcmWay1_HW			= 1,		
	eAcmWay2_SW			= 2,		
}ACM_METHOD,*PACM_METHOD;


typedef struct _ACM{
	u64		UsedTime;
	u64		MediumTime;
	u8		HwAcmCtl;	
}ACM, *PACM;

typedef	u8		AC_UAPSD, *PAC_UAPSD;

#define	GET_VO_UAPSD(_apsd) ((_apsd) & BIT0)
#define	SET_VO_UAPSD(_apsd) ((_apsd) |= BIT0)
	
#define	GET_VI_UAPSD(_apsd) ((_apsd) & BIT1)
#define	SET_VI_UAPSD(_apsd) ((_apsd) |= BIT1)

#define	GET_BK_UAPSD(_apsd) ((_apsd) & BIT2)
#define	SET_BK_UAPSD(_apsd) ((_apsd) |= BIT2)

#define	GET_BE_UAPSD(_apsd) ((_apsd) & BIT3)
#define	SET_BE_UAPSD(_apsd) ((_apsd) |= BIT3)
	

typedef union _QOS_TCLAS{

	struct _TYPE_GENERAL{
		u8		Priority;
		u8 		ClassifierType;
		u8 		Mask;
	} TYPE_GENERAL;

	struct _TYPE0_ETH{
		u8		Priority;
		u8 		ClassifierType;
		u8 		Mask;
		u8		SrcAddr[6];
		u8		DstAddr[6];
		u16		Type;
	} TYPE0_ETH;

	struct _TYPE1_IPV4{
		u8		Priority;
		u8 		ClassifierType;
		u8 		Mask;
		u8 		Version;
		u8		SrcIP[4];
		u8		DstIP[4];
		u16		SrcPort;
		u16		DstPort;
		u8		DSCP;
		u8		Protocol;
		u8		Reserved;
	} TYPE1_IPV4;

	struct _TYPE1_IPV6{
		u8		Priority;
		u8 		ClassifierType;
		u8 		Mask;
		u8 		Version;
		u8		SrcIP[16];
		u8		DstIP[16];
		u16		SrcPort;
		u16		DstPort;
		u8		FlowLabel[3];
	} TYPE1_IPV6;

	struct _TYPE2_8021Q{
		u8		Priority;
		u8 		ClassifierType;
		u8 		Mask;
		u16		TagType;
	} TYPE2_8021Q;
} QOS_TCLAS, *PQOS_TCLAS;

typedef struct _QOS_TSTREAM{
	u8			AC;
	WMM_TSPEC		TSpec;
	QOS_TCLAS		TClass;
} QOS_TSTREAM, *PQOS_TSTREAM;



typedef struct _OCTET_STRING{
        u8        	*Octet;
        u16             Length;
}OCTET_STRING, *POCTET_STRING;
#if 0
#define FillOctetString(_os,_octet,_len)             \
        (_os).Octet=(u8 *)(_octet);                  \
        (_os).Length=(_len);

#define WMM_ELEM_HDR_LEN        		     6
#define WMMElemSkipHdr(_osWMMElem)                   \
        (_osWMMElem).Octet += WMM_ELEM_HDR_LEN;      \
        (_osWMMElem).Length -= WMM_ELEM_HDR_LEN;
#endif
typedef struct _STA_QOS{
	u8				WMMIEBuf[MAX_WMMELE_LENGTH];
	u8*				WMMIE;

	QOS_MODE			QosCapability; 
	QOS_MODE			CurrentQosMode;

	AC_UAPSD			b4ac_Uapsd;  
	AC_UAPSD			Curr4acUapsd;
	u8				bInServicePeriod;
	u8				MaxSPLength;
	int 				NumBcnBeforeTrigger;

	u8 *				pWMMInfoEle;
	u8				WMMParamEle[WMM_PARAM_ELEMENT_SIZE];
	u8				WMMPELength;

	QOS_INFO_FIELD			QosInfoField_STA; 	
	QOS_INFO_FIELD			QosInfoField_AP;	

	AC_PARAM			CurAcParameters[4];

	ACM				acm[4];
	ACM_METHOD			AcmMethod;

	QOS_TSTREAM			TStream[16];
	WMM_TSPEC			TSpec;

	u32				QBssWirelessMode;

	u8				bNoAck;

	u8				bEnableRxImmBA;

}STA_QOS, *PSTA_QOS;

typedef struct _BSS_QOS{
	QOS_MODE		bdQoSMode;

	u8			bdWMMIEBuf[MAX_WMMELE_LENGTH];
	u8*		bdWMMIE;

	QOS_ELE_SUBTYPE		EleSubType;

	u8 *			pWMMInfoEle;
	u8 *			pWMMParamEle;
	
	QOS_INFO_FIELD		QosInfoField;
	AC_PARAM		AcParameter[4];
}BSS_QOS, *PBSS_QOS;


#define sQoSCtlLng			2
#define	QOS_CTRL_LEN(_QosMode)		((_QosMode > QOS_DISABLE)? sQoSCtlLng : 0)


#define IsACValid(ac)			((ac<=7 )?true:false )

#endif 
