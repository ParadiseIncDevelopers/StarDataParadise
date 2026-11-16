#include "routine_export.hpp"

#include <QDialog>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace wb
{
namespace
{
constexpr auto java = R"java(// Maven: com.stardataparadise:stardataparadise-jdbc:0.2.0
import com.stardataparadise.sdk.SdpConnection;
import java.sql.*;
import java.util.*;

public class Example {
    static String config(String key) {
        return Objects.requireNonNull(System.getenv(key), key);
    }

    public static void main(String[] args) throws SQLException {
        var tls = new Properties();
        tls.setProperty("user", config("SDP_USER"));
        tls.setProperty("password", config("SDP_PASSWORD"));
        tls.setProperty("caFile", config("SDP_CA"));
        tls.setProperty("tlsName", config("SDP_TLS_NAME"));
        try (var db = DriverManager.getConnection(config("SDP_JDBC_URL"), tls)) {
            db.setAutoCommit(false);
            try {
                try (var stmt = db.prepareStatement("SELECT note FROM items WHERE id = ?")) {
                    stmt.setLong(1, 7);
                    try (var rows = stmt.executeQuery()) {
                        while (rows.next()) System.out.println(rows.getString(1));
                    }
                }
                try (var call = db.prepareCall("{call list_items(?,?)}")) {
                    call.setLong(1, 7);
                    call.registerOutParameter(2, Types.BIGINT);
                    call.execute();
                    System.out.println(call.getObject(2));
                }
                db.rollback();
            } catch (SQLException failure) {
                if (!db.isClosed()) db.rollback();
                throw failure;
            }
            db.setAutoCommit(true);
            var sdp = db.unwrap(SdpConnection.class);
            System.out.println(sdp.predict("sales_forecast", Map.of("reading", 17)).value());
            System.out.println(sdp.historyRead("items", config("SDP_REVISION"), config("SDP_ROW_ID")));
        }
    }
}
)java";

constexpr auto dotnet = R"csharp(// .NET 10: StarDataParadiseDrive 2.0.0-preview.1
// .NET Framework 4.8: StarDataParadiseNetFramework 2.0.0-preview.1
using System;
using System.Collections.Generic;
using System.Data;
using StarDataParadise.SDK;

public static class Example
{
    static string Config(string key)
    {
        return Environment.GetEnvironmentVariable(key)
            ?? throw new InvalidOperationException(key);
    }

    public static void Main()
    {
        var options = SdpFactory.Instance.CreateConnectionStringBuilder();
        options["Host"] = Config("SDP_HOST");
        options["Port"] = Config("SDP_PORT");
        options["User"] = Config("SDP_USER");
        options["Password"] = Config("SDP_PASSWORD");
        options["CA File"] = Config("SDP_CA");
        options["TLS Name"] = Config("SDP_TLS_NAME");
        using (var db = new SdpConnection(options.ConnectionString))
        {
            db.Open();
            using (var tx = db.BeginTransaction())
            {
                using (var query = db.CreateCommand())
                {
                    query.Transaction = tx;
                    query.CommandText = "SELECT note FROM items WHERE id = :id";
                    query.Parameters.Add(new SdpParameter("id", 7L));
                    using (var rows = query.ExecuteReader())
                        while (rows.Read()) Console.WriteLine(rows.GetValue(0));
                }
                using (var call = db.CreateCommand())
                {
                    call.Transaction = tx;
                    call.CommandText = "CALL list_items(:id, NULL)";
                    call.Parameters.Add(new SdpParameter("id", 7L));
                    using (var outputs = call.ExecuteReader())
                        if (outputs.Read()) Console.WriteLine(outputs.GetValue(0));
                }
                tx.Rollback();
            }
            Console.WriteLine(db.PredictAsync("sales_forecast",
                new Dictionary<string, object> { { "reading", 17 } }).GetAwaiter().GetResult());
            Console.WriteLine(db.GetHistoryAsync("items", 10).GetAwaiter().GetResult());
        }
    }
}
)csharp";

constexpr auto python = R"python(# Python 3.12: stardataparadise-sdk==0.2.0
import os
from stardataparadise_sdk import connect

config = dict(
    host=os.environ["SDP_HOST"],
    port=int(os.environ["SDP_PORT"]),
    user=os.environ["SDP_USER"],
    password=os.environ["SDP_PASSWORD"],
    ca_file=os.environ["SDP_CA"],
    tls_name=os.environ["SDP_TLS_NAME"],
)

with connect(**config) as db:
    with db.cursor() as cursor:
        cursor.execute("SELECT note FROM items WHERE id = :id", {"id": 7})
        print(cursor.fetchmany(16))
        cursor.callproc("list_items", (7, None))
        print(cursor.fetchone())
        while cursor.nextset():
            print(cursor.set_name, cursor.fetchmany(16))
    db.rollback()

with connect(**config, autocommit=True) as db:
    print(db.predict("sales_forecast", {"reading": 17}))
    print(db.history("items", limit=10))
)python";
}

void showSdkExamples(QWidget* parent)
{
    auto* window = new QDialog(parent);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QObject::tr("StarDataParadise SDK examples"));
    window->resize(740, 570);
    auto* layout = new QVBoxLayout(window);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs);
    const auto tab = [tabs](const QString& title, const char* source) {
        auto* code = new QPlainTextEdit(QString::fromUtf8(source));
        code->setReadOnly(true);
        tabs->addTab(code, title);
    };
    tab(QStringLiteral("Java / JDBC"), java);
    tab(QStringLiteral(".NET / .NET Framework"), dotnet);
    tab(QStringLiteral("Python"), python);
    window->show();
}
}
