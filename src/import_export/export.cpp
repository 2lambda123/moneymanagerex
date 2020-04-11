/*******************************************************
Copyright (C) 2013 Nikolay

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
********************************************************/

#include "export.h"
#include "constants.h"
#include "paths.h"
#include "util.h"
#include "model/Model_Account.h"
#include "model/Model_Attachment.h"
#include "model/Model_Category.h"
#include "model/Model_Checking.h"
#include "model/Model_Currency.h"
#include "model/Model_CustomField.h"
#include "model/Model_Payee.h"
#include "model/Model_CustomFieldData.h"
#include "model/Model_CustomField.h"


mmExportTransaction::mmExportTransaction()
{}

mmExportTransaction::~mmExportTransaction()
{}

const wxString mmExportTransaction::getTransactionQIF(const Model_Checking::Full_Data& full_tran
    , const wxString& dateMask, bool reverce)
{
    bool transfer = Model_Checking::is_transfer(full_tran.TRANSCODE);

    wxString buffer = "";
    wxString categ = full_tran.m_splits.empty() ? full_tran.CATEGNAME : "";
    wxString transNum = full_tran.TRANSACTIONNUMBER;
    wxString notes = (full_tran.NOTES);
    wxString payee = full_tran.PAYEENAME;

    if (transfer)
    {
        const auto acc_in = Model_Account::instance().get(full_tran.ACCOUNTID);
        const auto acc_to = Model_Account::instance().get(full_tran.TOACCOUNTID);
        const auto curr_in = Model_Currency::instance().get(acc_in->CURRENCYID);
        const auto curr_to = Model_Currency::instance().get(acc_to->CURRENCYID);

        categ = "[" + (reverce ? full_tran.ACCOUNTNAME : full_tran.TOACCOUNTNAME) + "]";
        payee = wxString::Format("%s %s %s -> %s %s %s"
            , wxString::FromCDouble(full_tran.TRANSAMOUNT, 2), curr_in->CURRENCY_SYMBOL, acc_in->ACCOUNTNAME
            , wxString::FromCDouble(full_tran.TOTRANSAMOUNT, 2), curr_to->CURRENCY_SYMBOL, acc_to->ACCOUNTNAME);
        //Transaction number used to make transaction unique
        // to proper merge transfer records
        if (transNum.IsEmpty() && notes.IsEmpty())
            transNum = wxString::Format("#%i", full_tran.id());
    }

    buffer << "D" << Model_Checking::TRANSDATE(full_tran).Format(dateMask) << "\n";
    buffer << "C" << (full_tran.STATUS == "R" ? "R" : "") << "\n";
    double value = Model_Checking::balance(full_tran
        , (reverce ? full_tran.TOACCOUNTID : full_tran.ACCOUNTID));
    const wxString& s = wxString::FromCDouble(value, 2);
    buffer << "T" << s << "\n";
    if (!payee.empty())
        buffer << "P" << payee << "\n";
    if (!transNum.IsEmpty())
        buffer << "N" << transNum << "\n";
    if (!categ.IsEmpty())
        buffer << "L" << categ << "\n";
    if (!notes.IsEmpty())
    {
        notes.Replace("''", "'");
        notes.Replace("\n", "\nM");
        buffer << "M" << notes << "\n";
    }

    for (const auto &split_entry : full_tran.m_splits)
    {
        double valueSplit = split_entry.SPLITTRANSAMOUNT;
        if (Model_Checking::type(full_tran) == Model_Checking::WITHDRAWAL)
            valueSplit = -valueSplit;
        const wxString split_amount = wxString::FromCDouble(valueSplit, 2);
        const wxString split_categ = Model_Category::full_name(split_entry.CATEGID, split_entry.SUBCATEGID);
        buffer << "S" << split_categ << "\n"
            << "$" << split_amount << "\n";
    }

    buffer << "^" << "\n";
    return buffer;
}

const wxString mmExportTransaction::getAccountHeaderQIF(int accountID)
{
    wxString buffer = "";
    wxString currency_symbol = Model_Currency::GetBaseCurrency()->CURRENCY_SYMBOL;
    Model_Account::Data *account = Model_Account::instance().get(accountID);
    if (account)
    {
        double dInitBalance = account->INITIALBAL;
        Model_Currency::Data *currency = Model_Currency::instance().get(account->CURRENCYID);
        if (currency)
        {
            currency_symbol = currency->CURRENCY_SYMBOL;
        }

        const wxString currency_code = "[" + currency_symbol + "]";
        const wxString sInitBalance = Model_Currency::toString(dInitBalance, currency);

        buffer = wxString("!Account") << "\n"
            << "N" << account->ACCOUNTNAME << "\n"
            << "T" << qif_acc_type(account->ACCOUNTTYPE) << "\n"
            << "D" << currency_code << "\n"
            << (dInitBalance != 0 ? wxString::Format("$%s\n", sInitBalance) : "")
            << "^" << "\n"
            << "!Type:Cash" << "\n";
    }

    return buffer;
}

const wxString mmExportTransaction::getCategoriesQIF()
{
    wxString buffer_qif = "";

    buffer_qif << "!Type:Cat" << "\n";
    for (const auto& category: Model_Category::instance().all())
    {
        const wxString& categ_name = category.CATEGNAME;
        bool bIncome = Model_Category::has_income(category.CATEGID);
        buffer_qif << "N" << categ_name <<  "\n"
            << (bIncome ? "I" : "E") << "\n"
            << "^" << "\n";

        for (const auto& sub_category: Model_Category::sub_category(category))
        {
            bIncome = Model_Category::has_income(category.CATEGID, sub_category.SUBCATEGID);
            bool bSubcateg = sub_category.CATEGID != -1;
            wxString full_categ_name = wxString()
                << categ_name << (bSubcateg ? wxString()<<":" : wxString()<<"")
                << sub_category.SUBCATEGNAME;
            buffer_qif << "N" << full_categ_name << "\n"
                << (bIncome ? "I" : "E") << "\n"
                << "^" << "\n";
        }
    }
    return buffer_qif;
}

//map Quicken !Account type strings to Model_Account::TYPE
// (not sure whether these need to be translated)
const std::unordered_map<wxString, int> mmExportTransaction::m_QIFaccountTypes =
{
    std::make_pair(wxString("Cash"), Model_Account::CASH), //Cash Flow: Cash Account
    std::make_pair(wxString("Bank"), Model_Account::CHECKING), //Cash Flow: Checking Account
    std::make_pair(wxString("CCard"), Model_Account::CREDIT_CARD), //Cash Flow: Credit Card Account
    std::make_pair(wxString("Invst"), Model_Account::INVESTMENT), //Investing: Investment Account
    std::make_pair(wxString("Oth A"), Model_Account::CHECKING), //Property & Debt: Asset
    std::make_pair(wxString("Oth L"), Model_Account::CHECKING), //Property & Debt: Liability
    std::make_pair(wxString("Invoice"), Model_Account::CHECKING), //Invoice (Quicken for Business only)
};

const wxString mmExportTransaction::qif_acc_type(const wxString& mmex_type)
{
    wxString qif_acc_type = m_QIFaccountTypes.begin()->first;
    for (const auto &item : m_QIFaccountTypes)
    {
        if (item.second == Model_Account::all_type().Index(mmex_type))
        {
            qif_acc_type = item.first;
            break;
        }
    }
    return qif_acc_type;
}

const wxString mmExportTransaction::mm_acc_type(const wxString& qif_type)
{
    wxString mm_acc_type = Model_Account::all_type()[Model_Account::CASH];
    for (const auto &item : m_QIFaccountTypes)
    {
        if (item.first == qif_type)
        {
            mm_acc_type = Model_Account::all_type()[(item.second)];
            break;
        }
    }
    return mm_acc_type;
}

// JSON Export ----------------------------------------------------------------------------

void mmExportTransaction::getAccountsJSON(PrettyWriter<StringBuffer>& json_writer, std::unordered_map <int /*account ID*/, wxString>& allAccounts4Export)
{
    json_writer.Key("accounts");
    json_writer.StartArray();
    for (const auto &entry : allAccounts4Export)
    {
        Model_Account::Data* a = Model_Account::instance().get(entry.first);
        const auto c = Model_Currency::instance().get(a->CURRENCYID);
        json_writer.StartObject();
        json_writer.Key("id");
        json_writer.Int(a->ACCOUNTID);
        json_writer.Key("name");
        json_writer.String(a->ACCOUNTNAME.c_str());
        json_writer.Key("initialBalance");
        json_writer.Double(a->INITIALBAL);
        json_writer.Key("type");
        json_writer.String(a->ACCOUNTTYPE.c_str());
        json_writer.Key("currency");
        json_writer.String(c->CURRENCY_SYMBOL.c_str());
        json_writer.EndObject();
    }
    json_writer.EndArray();
}

void mmExportTransaction::getPayeesJSON(PrettyWriter<StringBuffer>& json_writer, wxArrayInt& allPayeess4Export)
{
    if (!allPayeess4Export.empty())
    {
        json_writer.Key("payees");
        json_writer.StartArray();
        for (const auto& entry : allPayeess4Export) {
            Model_Payee::Data* p = Model_Payee::instance().get(entry);
            if (p) {
                json_writer.StartObject();
                json_writer.Key("id");
                json_writer.Int(p->PAYEEID);
                json_writer.Key("name");
                json_writer.String(p->PAYEENAME.c_str());
                json_writer.EndObject();
            }
        }
        json_writer.EndArray();
    }
}

void mmExportTransaction::getCategoriesJSON(PrettyWriter<StringBuffer>& json_writer)
{
    json_writer.Key("categories");
    json_writer.StartArray();
    for (const auto& category : Model_Category::instance().all())
    {
        json_writer.StartObject();
        json_writer.Key("id");
        json_writer.Int(category.CATEGID);
        json_writer.Key("name");
        json_writer.String(category.CATEGNAME.c_str());

        const auto sub_categ = Model_Category::sub_category(category);
        if (!sub_categ.empty())
        {
            json_writer.Key("sub_categories");
            json_writer.StartArray();
            for (const auto& sub_category : sub_categ)
            {
                auto test = sub_categ.to_json();
                json_writer.StartObject();
                json_writer.Key("id");
                json_writer.Int(sub_category.CATEGID);
                json_writer.Key("name");
                json_writer.String(sub_category.SUBCATEGNAME.c_str());
                json_writer.EndObject();
            }
            json_writer.EndArray();
        }
        json_writer.EndObject();
    }
    json_writer.EndArray();
}

void mmExportTransaction::getTransactionJSON(PrettyWriter<StringBuffer>& json_writer, const Model_Checking::Full_Data& full_tran)
{
    json_writer.StartObject();
    full_tran.as_json(json_writer);

    if (!full_tran.m_splits.empty()) {
        json_writer.Key("division");
        json_writer.StartArray();
        for (const auto &split_entry : full_tran.m_splits)
        {
            double valueSplit = split_entry.SPLITTRANSAMOUNT;
            if (Model_Checking::type(full_tran) == Model_Checking::WITHDRAWAL)
                valueSplit = -valueSplit;
            const wxString split_amount = wxString::FromCDouble(valueSplit, 2);
            const wxString split_categ = Model_Category::full_name(split_entry.CATEGID, split_entry.SUBCATEGID);

            json_writer.StartObject();
            json_writer.Key("category_id");
            json_writer.Int(split_entry.CATEGID);
            json_writer.Key("sub_category_id");
            json_writer.Int(split_entry.SUBCATEGID);
            json_writer.Key("amount");
            json_writer.Double(valueSplit);
            json_writer.EndObject();

        }
        json_writer.EndArray();
    }

    const wxString RefType = Model_Attachment::reftype_desc(Model_Attachment::TRANSACTION);
    Model_Attachment::Data_Set attachments = Model_Attachment::instance().FilterAttachments(RefType, full_tran.id());

    if (!attachments.empty())
    {
        const wxString folder = Model_Infotable::instance().GetStringInfo("ATTACHMENTSFOLDER:" + mmPlatformType(), "");
        const wxString AttachmentsFolder = mmex::getPathAttachment(folder);
        json_writer.Key("ATTACHMENTS");
        json_writer.StartArray();
        for (const auto &entry : attachments)
        {
            json_writer.Int(entry.ATTACHMENTID);
        }
        json_writer.EndArray();

    }

    auto data = Model_CustomFieldData::instance().find(Model_CustomFieldData::REFID(full_tran.id()));
    auto f = Model_CustomField::instance().find(Model_CustomField::REFTYPE(RefType));
    if (!data.empty())
    {
        json_writer.Key("CUSTOM_FIELDS");
        json_writer.StartArray();
        for (const auto &entry : data)
        {

            auto field = Model_CustomField::instance().find(
                Model_CustomField::REFTYPE(RefType)
                , Model_CustomField::FIELDID(entry.FIELDID));

            for (const auto& i : field)
            {
                json_writer.Int(i.FIELDID);
            }
        }
        json_writer.EndArray();
    }

    json_writer.EndObject();
}

void mmExportTransaction::getAttachmentsJSON(PrettyWriter<StringBuffer>& json_writer, wxArrayInt& allAttachment4Export)
{

    if (!allAttachment4Export.empty())
    {
        const wxString RefType = Model_Attachment::reftype_desc(Model_Attachment::TRANSACTION);
        const wxString folder = Model_Infotable::instance().GetStringInfo("ATTACHMENTSFOLDER:" + mmPlatformType(), "");
        const wxString AttachmentsFolder = mmex::getPathAttachment(folder);

        json_writer.Key("attachments");
        json_writer.StartObject();

        json_writer.Key("folder");
        json_writer.String(folder.c_str());
        json_writer.Key("full_path");
        json_writer.String(AttachmentsFolder.c_str());
        json_writer.Key("reference_type");
        json_writer.String(RefType.c_str());

        json_writer.Key("attachments_data");
        json_writer.StartArray();

        Model_Attachment::Data_Set attachments = Model_Attachment::instance().all();
        for (const auto& entry : attachments)
        {
            if (entry.REFTYPE != RefType) continue;
            if (allAttachment4Export.Index(entry.REFID) == wxNOT_FOUND) continue;

            json_writer.StartObject();

            json_writer.Key("id");
            json_writer.Int(entry.ATTACHMENTID);
            json_writer.Key("description");
            json_writer.String(entry.DESCRIPTION.c_str());
            json_writer.Key("file_name");
            json_writer.String(entry.FILENAME.c_str());

            json_writer.EndObject();
        }
        json_writer.EndArray();
        json_writer.EndObject();
    }
}



void mmExportTransaction::getCustomFieldsJSON(PrettyWriter<StringBuffer>& json_writer, wxArrayInt& allCustomFields4Export)
{

    if (!allCustomFields4Export.empty())
    {
        const wxString RefType = Model_Attachment::reftype_desc(Model_Attachment::TRANSACTION);

        json_writer.Key("custom_fields");
        json_writer.StartObject();
        
        // Data
        json_writer.Key("custom_fields_data");
        json_writer.StartArray();

        wxArrayInt cd;
        Model_CustomFieldData::Data_Set cds = Model_CustomFieldData::instance().all();
        for (const auto & entry : cds)
        {
            if (allCustomFields4Export.Index(entry.FIELDATADID) != wxNOT_FOUND)
                if (cd.Index(entry.FIELDID) == wxNOT_FOUND) 
                    cd.Add(entry.FIELDID);

            json_writer.StartObject();
            json_writer.Key("FIELDID");
            json_writer.Int(entry.FIELDID);
            json_writer.Key("FIELDATADID");
            json_writer.Int(entry.FIELDATADID);
            json_writer.Key("REFID");
            json_writer.Int(entry.REFID);
            json_writer.Key("CONTENT");
            json_writer.String(entry.CONTENT.c_str());
            json_writer.EndObject();

        }
        json_writer.EndArray();

        //Settings
        json_writer.Key("custom_fields_settings");
        json_writer.StartArray();

        Model_CustomField::Data_Set custom_fields = Model_CustomField::instance().find(Model_CustomField::DB_Table_CUSTOMFIELD_V1::REFTYPE(RefType));
        for (const auto& entry : custom_fields)
        {
            if (entry.REFTYPE != RefType) continue;
            if (cd.Index(entry.FIELDID) == wxNOT_FOUND) continue;
            json_writer.StartObject();

            json_writer.Key("id");
            json_writer.Int(entry.FIELDID);
            json_writer.Key("reference_type");
            json_writer.String(entry.REFTYPE.c_str());
            json_writer.Key("description");
            json_writer.String(entry.DESCRIPTION.c_str());
            json_writer.Key("type");
            json_writer.String(entry.TYPE.c_str());
            json_writer.Key("properties");
            json_writer.RawValue(entry.PROPERTIES.c_str(), entry.PROPERTIES.size(), rapidjson::Type::kObjectType);

            json_writer.EndObject();
        }
        json_writer.EndArray();
        json_writer.EndObject();

    }
}