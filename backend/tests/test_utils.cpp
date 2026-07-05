#include "test_framework.h"
#include "utils.h"

using namespace studio;

// =========================================================================
// errorText
// =========================================================================

TEST(errorText_OK) {
    EXPECT_STREQ(errorText(IED_ERROR_OK), "OK");
}

TEST(errorText_NotConnected) {
    EXPECT_STREQ(errorText(IED_ERROR_NOT_CONNECTED), "NOT_CONNECTED");
}

TEST(errorText_ConnectionLost) {
    EXPECT_STREQ(errorText(IED_ERROR_CONNECTION_LOST), "CONNECTION_LOST");
}

TEST(errorText_UnknownReturnsNumeric) {
    std::string result = errorText(static_cast<IedClientError>(9999));
    EXPECT_CONTAINS(result, "IED_ERROR_");
    EXPECT_CONTAINS(result, "9999");
}

// =========================================================================
// fcText / fcFromText round-trip
// =========================================================================

TEST(fcText_Basic) {
    EXPECT_STREQ(fcText(IEC61850_FC_ST), "ST");
    EXPECT_STREQ(fcText(IEC61850_FC_MX), "MX");
    EXPECT_STREQ(fcText(IEC61850_FC_DC), "DC");
    EXPECT_STREQ(fcText(IEC61850_FC_SP), "SP");
    EXPECT_STREQ(fcText(IEC61850_FC_CO), "CO");
    EXPECT_STREQ(fcText(IEC61850_FC_RP), "RP");
    EXPECT_STREQ(fcText(IEC61850_FC_BR), "BR");
}

TEST(fcFromText_Basic) {
    EXPECT_TRUE(fcFromText("ST") == IEC61850_FC_ST);
    EXPECT_TRUE(fcFromText("MX") == IEC61850_FC_MX);
    EXPECT_TRUE(fcFromText("DC") == IEC61850_FC_DC);
    EXPECT_TRUE(fcFromText("CO") == IEC61850_FC_CO);
    EXPECT_TRUE(fcFromText("BR") == IEC61850_FC_BR);
}

TEST(fcFromText_UnknownReturnsMx) {
    // Unknown FC strings default to MX per implementation
    EXPECT_TRUE(fcFromText("ZZ") == IEC61850_FC_MX);
    EXPECT_TRUE(fcFromText("") == IEC61850_FC_MX);
}

TEST(fc_RoundTrip) {
    // All known FCs should round-trip: text -> enum -> text
    const std::vector<std::string> fcs = {
        "ST", "MX", "SP", "SV", "CF", "DC", "SG", "SE", "SR", "OR",
        "BL", "EX", "CO", "US", "MS", "RP", "BR", "LG", "GO"
    };
    for (const auto& fc : fcs) {
        FunctionalConstraint enumVal = fcFromText(fc);
        EXPECT_STREQ(fcText(enumVal), fc);
    }
}

// =========================================================================
// controlModelFromText
// =========================================================================

TEST(controlModel_DirectNormal) {
    EXPECT_TRUE(controlModelFromText("direct") == CONTROL_MODEL_DIRECT_NORMAL);
    EXPECT_TRUE(controlModelFromText("") == CONTROL_MODEL_DIRECT_NORMAL);
    EXPECT_TRUE(controlModelFromText("unknown") == CONTROL_MODEL_DIRECT_NORMAL);
}

TEST(controlModel_SboNormal) {
    EXPECT_TRUE(controlModelFromText("sbo") == CONTROL_MODEL_SBO_NORMAL);
    EXPECT_TRUE(controlModelFromText("select-before-operate") == CONTROL_MODEL_SBO_NORMAL);
}

TEST(controlModel_SboEnhanced) {
    EXPECT_TRUE(controlModelFromText("enhanced-sbo") == CONTROL_MODEL_SBO_ENHANCED);
}

TEST(controlModel_DirectEnhanced) {
    EXPECT_TRUE(controlModelFromText("enhanced-direct") == CONTROL_MODEL_DIRECT_ENHANCED);
}

// =========================================================================
// parseBitMask
// =========================================================================

TEST(parseBitMask_Decimal) {
    EXPECT_EQ(parseBitMask("16", 0), 16u);
    EXPECT_EQ(parseBitMask("255", 0), 255u);
}

TEST(parseBitMask_Hex) {
    EXPECT_EQ(parseBitMask("0xFF", 0), 255u);
    EXPECT_EQ(parseBitMask("0x10", 0), 16u);
}

TEST(parseBitMask_EmptyReturnsFallback) {
    EXPECT_EQ(parseBitMask("", 42), 42u);
}

TEST(parseBitMask_InvalidReturnsFallback) {
    EXPECT_EQ(parseBitMask("abc", 7), 7u);
}

TEST(parseBitMask_DefaultFallbackIsZero) {
    EXPECT_EQ(parseBitMask("abc"), 0u);
}

// =========================================================================
// reasonText
// =========================================================================

TEST(reasonText_NotIncluded) {
    EXPECT_STREQ(reasonText(IEC61850_REASON_NOT_INCLUDED), "not-included");
}

TEST(reasonText_DataChange) {
    EXPECT_STREQ(reasonText(IEC61850_REASON_DATA_CHANGE), "data-change");
}

TEST(reasonText_MultipleFlags) {
    std::string result = reasonText(
        IEC61850_REASON_DATA_CHANGE | IEC61850_REASON_QUALITY_CHANGE);
    EXPECT_CONTAINS(result, "data-change");
    EXPECT_CONTAINS(result, "quality-change");
}

TEST(reasonText_Integrity) {
    EXPECT_STREQ(reasonText(IEC61850_REASON_INTEGRITY), "integrity");
}

// =========================================================================
// dotReference
// =========================================================================

TEST(dotReference_Basic) {
    EXPECT_STREQ(dotReference("LD$LN$DO"), "LD.LN.DO");
    EXPECT_STREQ(dotReference("DemoIEDLD0$LLN0$Mod"), "DemoIEDLD0.LLN0.Mod");
}

TEST(dotReference_NoDollarSigns) {
    EXPECT_STREQ(dotReference("LD.LN.DO"), "LD.LN.DO");
}

TEST(dotReference_Empty) {
    EXPECT_STREQ(dotReference(""), "");
}

// =========================================================================
// datasetReferenceToMmsReference / logReferenceToMmsReference
// =========================================================================

TEST(datasetReferenceToMmsReference_Basic) {
    EXPECT_STREQ(datasetReferenceToMmsReference("LD/LN.ds"), "LD/LN$ds");
    EXPECT_STREQ(datasetReferenceToMmsReference("LD/LN.DataSet1"), "LD/LN$DataSet1");
}

TEST(datasetReferenceToMmsReference_NoDot) {
    EXPECT_STREQ(datasetReferenceToMmsReference("nodot"), "nodot");
}

TEST(logReferenceToMmsReference_Basic) {
    EXPECT_STREQ(logReferenceToMmsReference("LD/LN.EventLog"), "LD/LN$EventLog");
}

// =========================================================================
// logicalDeviceOf / logicalNodeNameOf
// =========================================================================

TEST(logicalDeviceOf_Basic) {
    EXPECT_STREQ(logicalDeviceOf("DemoIEDLD0/LLN0"), "DemoIEDLD0");
    EXPECT_STREQ(logicalDeviceOf("RelayLD/PDIS1"), "RelayLD");
}

TEST(logicalDeviceOf_NoSlash) {
    EXPECT_STREQ(logicalDeviceOf("LLN0"), "LLN0");
}

TEST(logicalNodeNameOf_Basic) {
    EXPECT_STREQ(logicalNodeNameOf("DemoIEDLD0/LLN0"), "LLN0");
    EXPECT_STREQ(logicalNodeNameOf("RelayLD/PDIS1"), "PDIS1");
}

TEST(logicalNodeNameOf_NoSlash) {
    EXPECT_STREQ(logicalNodeNameOf("LLN0"), "LLN0");
}

// =========================================================================
// dataSetReferenceForNode
// =========================================================================

TEST(dataSetReferenceForNode_EmptyDataSetName) {
    EXPECT_STREQ(dataSetReferenceForNode("LD/LLN0", ""), "");
}

TEST(dataSetReferenceForNode_AlreadyHasSlash) {
    EXPECT_STREQ(dataSetReferenceForNode("LD/LLN0", "LD/LLN0.Events"), "LD/LLN0.Events");
}

TEST(dataSetReferenceForNode_PrefixedWithLnName) {
    // value starts with "LLN0." -> prepend LD
    EXPECT_STREQ(dataSetReferenceForNode("DemoIEDLD0/LLN0", "LLN0.Events"), "DemoIEDLD0/LLN0.Events");
}

TEST(dataSetReferenceForNode_PrefixedWithFullLn) {
    // value starts with "LD/LLN0." -> return as-is
    EXPECT_STREQ(dataSetReferenceForNode("DemoIEDLD0/LLN0", "DemoIEDLD0/LLN0.Events"), "DemoIEDLD0/LLN0.Events");
}

TEST(dataSetReferenceForNode_BareName) {
    // bare name -> LN.name
    EXPECT_STREQ(dataSetReferenceForNode("DemoIEDLD0/LLN0", "Events"), "DemoIEDLD0/LLN0.Events");
}

TEST(dataSetReferenceForNode_EmptyLogicalNode) {
    EXPECT_STREQ(dataSetReferenceForNode("", "Events"), "Events");
}

// =========================================================================
// rcbReferenceForNode
// =========================================================================

TEST(rcbReferenceForNode_Unbuffered) {
    EXPECT_STREQ(rcbReferenceForNode("LD/LLN0", "EventsRCB01", false), "LD/LLN0.RP.EventsRCB01");
}

TEST(rcbReferenceForNode_Buffered) {
    EXPECT_STREQ(rcbReferenceForNode("LD/LLN0", "EventsBuffered01", true), "LD/LLN0.BR.EventsBuffered01");
}

TEST(rcbReferenceForNode_AlreadyHasRp) {
    EXPECT_STREQ(rcbReferenceForNode("LD/LLN0", "LD/LLN0.RP.EventsRCB01", false), "LD/LLN0.RP.EventsRCB01");
}

TEST(rcbReferenceForNode_AlreadyHasBr) {
    EXPECT_STREQ(rcbReferenceForNode("LD/LLN0", "LD/LLN0.BR.EventsBuffered01", true), "LD/LLN0.BR.EventsBuffered01");
}

TEST(rcbReferenceForNode_PrefixedWithRp) {
    EXPECT_STREQ(rcbReferenceForNode("LD/LLN0", "RP.EventsRCB01", false), "LD/LLN0.RP.EventsRCB01");
}

TEST(rcbReferenceForNode_EmptyName) {
    EXPECT_STREQ(rcbReferenceForNode("LD/LLN0", "", false), "");
}

// =========================================================================
// logicalNodeFromReportReference
// =========================================================================

TEST(logicalNodeFromReportReference_Urcb) {
    EXPECT_STREQ(logicalNodeFromReportReference("LD/LLN0.RP.EventsRCB01"), "LD/LLN0");
}

TEST(logicalNodeFromReportReference_Brcb) {
    EXPECT_STREQ(logicalNodeFromReportReference("LD/LLN0.BR.EventsBuffered01"), "LD/LLN0");
}

TEST(logicalNodeFromReportReference_DollarSign) {
    EXPECT_STREQ(logicalNodeFromReportReference("LD/LLN0$RP$EventsRCB01"), "LD/LLN0");
}

TEST(logicalNodeFromReportReference_NoMarker) {
    EXPECT_STREQ(logicalNodeFromReportReference("LD/LLN0"), "");
}

// =========================================================================
// reportHandlerReference
// =========================================================================

TEST(reportHandlerReference_StripsTrailingDigits) {
    EXPECT_STREQ(reportHandlerReference("LD/LLN0.RP.EventsRCB01"), "LD/LLN0.RP.EventsRCB");
}

TEST(reportHandlerReference_NoTrailingDigits) {
    EXPECT_STREQ(reportHandlerReference("LD/LLN0.RP.Events"), "LD/LLN0.RP.Events");
}

TEST(reportHandlerReference_DollarSigns) {
    EXPECT_STREQ(reportHandlerReference("LD/LLN0$RP$EventsRCB01"), "LD/LLN0.RP.EventsRCB");
}

// =========================================================================
// fcFromMemberReference
// =========================================================================

TEST(fcFromMemberReference_Basic) {
    EXPECT_STREQ(fcFromMemberReference("LD/LN.DO.da[MX]"), "MX");
    EXPECT_STREQ(fcFromMemberReference("LD/LN.DO.da[ST]"), "ST");
    EXPECT_STREQ(fcFromMemberReference("LD/LN.DO.da[CO]"), "CO");
}

TEST(fcFromMemberReference_NoBrackets) {
    EXPECT_STREQ(fcFromMemberReference("LD/LN.DO.da"), "");
}

TEST(fcFromMemberReference_Malformed) {
    EXPECT_STREQ(fcFromMemberReference("LD/LN.DO.da[MX"), "");
}

// =========================================================================
// stripFcSuffix
// =========================================================================

TEST(stripFcSuffix_Basic) {
    EXPECT_STREQ(stripFcSuffix("LD/LN.DO.da[MX]"), "LD/LN.DO.da");
    EXPECT_STREQ(stripFcSuffix("LD/LN.DO.da[ST]"), "LD/LN.DO.da");
}

TEST(stripFcSuffix_NoSuffix) {
    EXPECT_STREQ(stripFcSuffix("LD/LN.DO.da"), "LD/LN.DO.da");
}

TEST(stripFcSuffix_DollarSigns) {
    EXPECT_STREQ(stripFcSuffix("LD$LN$DO$da[MX]"), "LD.LN.DO.da");
}

TEST(stripFcSuffix_BracketNotAtEnd) {
    // bracket not at the very end -> keep as-is (dotReference only swaps $, not /)
    EXPECT_STREQ(stripFcSuffix("LD/LN.DO[MX].extra"), "LD/LN.DO[MX].extra");
}

// =========================================================================
// dataObjectReferenceFromMember
// =========================================================================

TEST(dataObjectReferenceFromMember_Basic) {
    // LD/LN.DO.da[FC] -> LD/LN.DO
    EXPECT_STREQ(dataObjectReferenceFromMember("DemoIEDLD0/LLN0.Health.stVal[ST]"), "DemoIEDLD0/LLN0.Health");
}

TEST(dataObjectReferenceFromMember_MagPath) {
    // LD/LN.DO.mag.f[FC] -> LD/LN.DO
    EXPECT_STREQ(dataObjectReferenceFromMember("DemoIEDLD0/MMXU1.TotW.mag.f[MX]"), "DemoIEDLD0/MMXU1.TotW");
}

TEST(dataObjectReferenceFromMember_NoDa) {
    // Only LD/LN.DO (no second dot) -> return as-is
    EXPECT_STREQ(dataObjectReferenceFromMember("DemoIEDLD0/LLN0.Health"), "DemoIEDLD0/LLN0.Health");
}

// =========================================================================
// tailName
// =========================================================================

TEST(tailName_DotSeparated) {
    EXPECT_STREQ(tailName("LD/LLN0.Health.stVal"), "stVal");
}

TEST(tailName_SlashSeparated) {
    EXPECT_STREQ(tailName("DemoIEDLD0/LLN0"), "LLN0");
}

TEST(tailName_WithFc) {
    EXPECT_STREQ(tailName("LD/LN.DO.da[MX]"), "da");
}

TEST(tailName_NoSeparator) {
    EXPECT_STREQ(tailName("simple"), "simple");
}

// =========================================================================
// collectFileBytes
// =========================================================================

TEST(collectFileBytes_Basic) {
    FileBuffer buf;
    uint8_t data[] = {0x48, 0x49, 0x50};  // "HIP"
    bool ok = collectFileBytes(&buf, data, 3);
    EXPECT_TRUE(ok);
    EXPECT_EQ(buf.data.size(), 3u);
    EXPECT_STREQ(buf.data, "HIP");
}

TEST(collectFileBytes_NullParam) {
    uint8_t data[] = {1, 2};
    bool ok = collectFileBytes(nullptr, data, 2);
    EXPECT_FALSE(ok);
}

TEST(collectFileBytes_NullBuffer) {
    FileBuffer buf;
    bool ok = collectFileBytes(&buf, nullptr, 2);
    EXPECT_FALSE(ok);
}

TEST(collectFileBytes_MultipleCalls) {
    FileBuffer buf;
    uint8_t chunk1[] = {'A', 'B'};
    uint8_t chunk2[] = {'C', 'D', 'E'};
    collectFileBytes(&buf, chunk1, 2);
    collectFileBytes(&buf, chunk2, 3);
    EXPECT_EQ(buf.data.size(), 5u);
    EXPECT_STREQ(buf.data, "ABCDE");
}

// =========================================================================
// timeTextFromMs
// =========================================================================

TEST(timeTextFromMs_Zero) {
    EXPECT_STREQ(timeTextFromMs(0), "");
}

TEST(timeTextFromMs_NonZero) {
    // Epoch start: 1970-01-01 00:00:00 (UTC; local may vary but format is consistent)
    std::string result = timeTextFromMs(1000);  // 1 second after epoch
    // Format should be "YYYY-MM-DD HH:MM:SS"
    EXPECT_EQ(result.size(), 19u);
    EXPECT_EQ(result[4], '-');
    EXPECT_EQ(result[7], '-');
    EXPECT_EQ(result[10], ' ');
    EXPECT_EQ(result[13], ':');
    EXPECT_EQ(result[16], ':');
}

// =========================================================================
// createMmsValueFromText (requires libiec61850 runtime)
// =========================================================================

TEST(createMmsValue_BooleanTrue) {
    MmsValue* v = createMmsValueFromText("boolean", "true");
    EXPECT_TRUE(v != nullptr);
    if (v) {
        EXPECT_STREQ(mmsTypeName(v), "BOOLEAN");
        MmsValue_delete(v);
    }
}

TEST(createMmsValue_BooleanFalse) {
    MmsValue* v = createMmsValueFromText("BOOLEAN", "false");
    EXPECT_TRUE(v != nullptr);
    if (v) MmsValue_delete(v);
}

TEST(createMmsValue_BooleanOne) {
    MmsValue* v = createMmsValueFromText("boolean", "1");
    EXPECT_TRUE(v != nullptr);
    if (v) MmsValue_delete(v);
}

TEST(createMmsValue_Float) {
    MmsValue* v = createMmsValueFromText("float", "3.14");
    EXPECT_TRUE(v != nullptr);
    if (v) {
        EXPECT_STREQ(mmsTypeName(v), "FLOAT");
        MmsValue_delete(v);
    }
}

TEST(createMmsValue_Integer) {
    MmsValue* v = createMmsValueFromText("integer", "42");
    EXPECT_TRUE(v != nullptr);
    if (v) {
        EXPECT_STREQ(mmsTypeName(v), "INTEGER");
        MmsValue_delete(v);
    }
}

TEST(createMmsValue_String) {
    MmsValue* v = createMmsValueFromText("string", "hello");
    EXPECT_TRUE(v != nullptr);
    if (v) MmsValue_delete(v);
}

TEST(createMmsValue_InvalidNumber) {
    MmsValue* v = createMmsValueFromText("float", "not_a_number");
    EXPECT_TRUE(v == nullptr);
}

TEST(createMmsValue_NullPointer) {
    EXPECT_TRUE(mmsValueToText(nullptr) == "<null>");
    EXPECT_TRUE(mmsTypeName(nullptr) == "");
}
