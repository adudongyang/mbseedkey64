#include "ecuseedkeydll.h"
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include <signal.h>

QString GetLastErrorAsString()
{
    auto errorMessageID = ::GetLastError();
    if(errorMessageID == 0)return QStringLiteral("No error message has been recorded");

    LPWSTR messageBuffer = Q_NULLPTR;
    size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), messageBuffer, 0, NULL);

    auto message = QString::fromWCharArray(messageBuffer, size);
    if(message.isEmpty())message.append(QString::number(errorMessageID));
    LocalFree(messageBuffer);

    return message;
}

ECUSeedKeyDLL::ECUSeedKeyDLL(QObject *parent) : QObject(parent),
    GetConfiguredAccessTypes(Q_NULLPTR), GetSeedLength(Q_NULLPTR), GetKeyLength(Q_NULLPTR), GetECUName(Q_NULLPTR),
    GetComment(Q_NULLPTR), GenerateKeyExOpt(Q_NULLPTR), GenerateKeyEx(Q_NULLPTR),
    p_dllHandle(Q_NULLPTR)
{
}

ECUSeedKeyDLL::ECUSeedKeyDLL(QString dll_path, QObject *parent) : QObject(parent),
    GetConfiguredAccessTypes(Q_NULLPTR), GetSeedLength(Q_NULLPTR), GetKeyLength(Q_NULLPTR), GetECUName(Q_NULLPTR),
    GetComment(Q_NULLPTR), GenerateKeyExOpt(Q_NULLPTR), GenerateKeyEx(Q_NULLPTR),
    p_dllHandle(Q_NULLPTR),
    p_dllPath(dll_path)
{
    if(dll_path.isEmpty())
    {
        this->setErrorMsg(tr("DLL path is empty!"));
        return;
    }

    QFileInfo info(this->p_dllPath);
    if(!info.exists() || !info.isFile() || info.isSymLink())
    {
        this->setErrorMsg(tr("DLL file doesn't exist or wrong."));
        return;
    }

    this->p_ecu_name = info.baseName();
    this->p_dll_name = info.fileName();
    emit DLLNameChanged();

    this->p_dllHandle = LoadLibraryW(QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString().data());
    if(!this->p_dllHandle)
    {
        auto error_str = GetLastErrorAsString();
        this->setErrorMsg(tr("Could not load DLL file. Reason: ")+error_str);
        return;
    }    
    this->loadDllfuncs();
}

ECUSeedKeyDLL::~ECUSeedKeyDLL()
{
    if(this->p_dllHandle != Q_NULLPTR)::FreeLibrary(this->p_dllHandle);
}

void ECUSeedKeyDLL::loadDllfuncs()
{
    this->GetECUName = (_f_GetECUName)GetProcAddress(this->p_dllHandle, "GetECUName");
    if(this->GetECUName != Q_NULLPTR)
    {
        this->p_ecu_name = this->GetECUName();
        emit this->ECUNameChanged();
    }

    this->GetComment = (_f_GetComment)GetProcAddress(this->p_dllHandle, "GetComment");
    if(this->GetComment != Q_NULLPTR)
    {
        this->p_comment = this->GetComment();
        emit this->CommentChanged();
    }

    this->GetSeedLength = (_f_GetSeedLength)GetProcAddress(this->p_dllHandle, "GetSeedLength");
    this->GetKeyLength = (_f_GetKeyLength)GetProcAddress(this->p_dllHandle, "GetKeyLength");
    this->GetConfiguredAccessTypes = (_f_GetConfiguredAccessTypes)GetProcAddress(this->p_dllHandle, "GetConfiguredAccessTypes");

    if(this->GetConfiguredAccessTypes != Q_NULLPTR)
    {
        int data[256] = {0};
        auto ret = this->GetConfiguredAccessTypes(data);
        if(ret > 0)
        {
            do
            {
                auto atype = data[--ret];
                ECUSeedKeyLenPairs skpair;
                skpair.seed_len = this->GetSeedLength ? this->GetSeedLength(atype) : 4;
                skpair.key_len  = this->GetKeyLength ? this->GetKeyLength(atype) : 4;
                this->p_access_types.insert(atype, skpair);
            }
            while(ret > 0);
            emit this->AccessTypesChanged();
        }
    }

    this->GenerateKeyExOpt = (_f_GenerateKeyExOpt)GetProcAddress(this->p_dllHandle, "GenerateKeyExOpt");
    this->GenerateKeyEx = (_f_GenerateKeyEx)GetProcAddress(this->p_dllHandle, "GenerateKeyEx");

    if(this->GenerateKeyExOpt == Q_NULLPTR && this->GenerateKeyEx == Q_NULLPTR)
    {
        this->setErrorMsg(tr("Neither GenerateKeyExOpt nor GenerateKeyEx found in DLL ") + this->p_dllPath);
        this->p_ecu_name = tr("not seedkey dll");
        emit ECUNameChanged();
        return;
    }
}

QString ECUSeedKeyDLL::AccessTypesString()
{
    if(this->AccessTypes().isEmpty())return QStringLiteral("not defined in dll");
    auto types = this->AccessTypes();
    QString str;
    foreach(auto t, types)
    {
        str = str.append(QStringLiteral("%1 ").arg(t, 2, 16, QLatin1Char('0'))).toUpper();
    }
    return str;
}

QStringList ECUSeedKeyDLL::AccessTypesStringList()
{
    QStringList list;
    foreach(auto a, this->AccessTypes())list << QStringLiteral("%1 ").arg(a, 2, 16, QLatin1Char('0')).toUpper();
    return list;
}

Q_INVOKABLE qint32 ECUSeedKeyDLL::seedLength(QString access_type)
{
    auto acc = access_type.toInt(Q_NULLPTR,16);
    if(this->p_access_types.contains(acc))
    {
        return this->p_access_types.value(acc).seed_len;
    }
    return 0;
}

Q_INVOKABLE qint32 ECUSeedKeyDLL::keyLength(QString access_type)
{
    auto acc = access_type.toInt(Q_NULLPTR,16);
    if(this->p_access_types.contains(acc))
    {
        return this->p_access_types.value(acc).key_len;
    }
    return 0;
}

QList<qint32> ECUSeedKeyDLL::generateKeyFromSeed(QList<qint32> seed, qint32 access_type, qint32 key_len)
{
    QList<qint32> key;
    if (this->GenerateKeyExOpt == Q_NULLPTR && this->GenerateKeyEx == Q_NULLPTR)
    {
        return key;
    }

    QByteArray seed_data(256, '\0');
    int seed_size = seed.size();
    if (seed_size > 256) seed_size = 256;
    for (int i = 0; i < seed_size; ++i) {
        seed_data[i] = static_cast<char>(seed.at(i) & 0xFF);
    }

    QByteArray key_data(256, '\0');
    unsigned int req_key_len = (key_len > 0 && key_len <= 256) ? static_cast<unsigned int>(key_len) : 64;
    unsigned int key_data_len = req_key_len;
    const char *empty_str = "";
    int ret = -1;

    if (this->GenerateKeyExOpt != Q_NULLPTR) {
        ret = this->GenerateKeyExOpt(
            reinterpret_cast<const unsigned char*>(seed_data.constData()),
            static_cast<unsigned int>(seed_size),
            static_cast<unsigned int>(access_type),
            empty_str,
            empty_str,
            reinterpret_cast<unsigned char*>(key_data.data()),
            256,
            key_data_len
        );
        // 如果传 256 失败，尝试精确传入界面指定的缓冲区长度 (如 64)
        if (ret != 0) {
            key_data_len = req_key_len;
            ret = this->GenerateKeyExOpt(
                reinterpret_cast<const unsigned char*>(seed_data.constData()),
                static_cast<unsigned int>(seed_size),
                static_cast<unsigned int>(access_type),
                empty_str,
                empty_str,
                reinterpret_cast<unsigned char*>(key_data.data()),
                req_key_len,
                key_data_len
            );
        }
    } else if (this->GenerateKeyEx != Q_NULLPTR) {
        ret = this->GenerateKeyEx(
            reinterpret_cast<const unsigned char*>(seed_data.constData()),
            static_cast<unsigned int>(seed_size),
            static_cast<unsigned int>(access_type),
            empty_str,
            reinterpret_cast<unsigned char*>(key_data.data()),
            256,
            key_data_len
        );
    }

    // 在界面 Comment 区域直接反馈调用状态码，方便排查
    this->p_comment = QString("Status: Ret=%1 | SeedLen=%2 | Acc=0x%3 | OutKeyLen=%4")
                          .arg(ret)
                          .arg(seed_size)
                          .arg(access_type, 2, 16, QLatin1Char('0')).toUpper()
                          .arg(key_data_len);
    emit this->CommentChanged();

    unsigned int final_len = key_data_len;
    if (final_len == 0 && key_len > 0) final_len = static_cast<unsigned int>(key_len);
    if (final_len > 256) final_len = 256;

    for (unsigned int c = 0; c < final_len; ++c) {
        key.append(static_cast<qint32>(static_cast<unsigned char>(key_data.at(static_cast<int>(c)))));
    }

    return key;
}

QString ECUSeedKeyDLL::generateKeyFromSeed(QString seed, QString access_type, QString key_len)
{
    bool ok_acc = false;
    bool ok_len = false;
    qint32 acc = access_type.trimmed().toInt(&ok_acc, 16);
    qint32 klen = key_len.trimmed().toInt(&ok_len, 10);
    return this->generateKeyFromSeed(seed, ok_acc ? acc : 0, ok_len ? klen : 0);
}

QString ECUSeedKeyDLL::generateKeyFromSeed(QString seed, qint32 access_type, qint32 key_len)
{
    QList<qint32> seed_list;
    QString clean_seed = seed.trimmed().remove(QChar(' '));
    for (int i = 0; i + 1 < clean_seed.length(); i += 2) {
        bool ok = false;
        int val = clean_seed.mid(i, 2).toInt(&ok, 16);
        if (ok) {
            seed_list.append(val);
        }
    }

    auto key = this->generateKeyFromSeed(seed_list, access_type, key_len);
    QString key_str;
    for (int i = 0; i < key.size(); ++i) {
        if (i > 0) key_str += QChar(' ');
        key_str += QStringLiteral("%1").arg(key.at(i) & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
    }
    return key_str;
}
