/***************************************************************************
    testqgsgdalguiutils.cpp
     --------------------------------------
    Date                 : 12.11.2024
    Copyright            : (C) 2024 Abdullaev Ruslan
    Email                : caboose7 at yandex dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstest.h"
#include "qgsgdalguiutils.h"
#include "qgsproject.h"
#include "qgsauthmethod.h"
#include "qgsauthconfig.h"
#include "qgsapplication.h"
#include "qgsauthmanager.h"

class TestQgsGdalGuiUtils : public QObject
{
    Q_OBJECT

  public:
    TestQgsGdalGuiUtils();

  private slots:
    void initTestCase();
    void cleanupTestCase();

    void checkUriBuilding();

  private:
    void cleanupTempDir();

    QList<QgsAuthMethodConfig> registerAuthConfigs();

  private:
    QString mTempDir;
    const char *mPass = nullptr;
};

TestQgsGdalGuiUtils::TestQgsGdalGuiUtils()
  : QObject()
  , mTempDir( QDir::tempPath() + "/auth" )
  , mPass( "pass" )
{
}

void TestQgsGdalGuiUtils::initTestCase()
{
  cleanupTempDir();

  // make QGIS_AUTH_DB_DIR_PATH temp dir for qgis-auth.db and master password file
  const QDir tmpDir = QDir::temp();
  QVERIFY2( tmpDir.mkpath( mTempDir ), "Couldn't make temp directory" );
  qputenv( "QGIS_AUTH_DB_DIR_PATH", mTempDir.toLatin1() );

  // init app and auth manager
  QgsApplication::init();
  QgsApplication::initQgis();
  QVERIFY2( !QgsApplication::authManager()->isDisabled(), "Authentication system is DISABLED" );

  // verify QGIS_AUTH_DB_DIR_PATH (temp auth db path) worked
  Q_NOWARN_DEPRECATED_PUSH
  const QString db1( QFileInfo( QgsApplication::authManager()->authenticationDatabasePath() ).canonicalFilePath() );
  Q_NOWARN_DEPRECATED_POP
  const QString db2( QFileInfo( mTempDir + "/qgis-auth.db" ).canonicalFilePath() );
  QCOMPARE( db1, db2 );

  // verify master pass can be set manually
  // (this also creates a fresh password hash in the new temp database)
  QVERIFY2( QgsApplication::authManager()->setMasterPassword( mPass, true ), "Master password could not be set" );
  QVERIFY2( QgsApplication::authManager()->masterPasswordIsSet(), "Auth master password not set from passed string" );

  // create QGIS_AUTH_PASSWORD_FILE file
  const QString passfilepath = mTempDir + "/passfile";
  QFile passfile( passfilepath );
  if ( passfile.open( QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate ) )
  {
    QTextStream fout( &passfile );
    fout << QString( mPass ) << "\r\n";
    passfile.close();
    qputenv( "QGIS_AUTH_PASSWORD_FILE", passfilepath.toLatin1() );
  }
  // qDebug( "QGIS_AUTH_PASSWORD_FILE=%s", qgetenv( "QGIS_AUTH_PASSWORD_FILE" ).constData() );

  // re-init app and auth manager
  QgsApplication::quit();
  // QTest::qSleep( 3000 );
  QgsApplication::init();
  QgsApplication::initQgis();
  QVERIFY2( !QgsApplication::authManager()->isDisabled(), "Authentication system is DISABLED" );

  // verify QGIS_AUTH_PASSWORD_FILE worked, when compared against hash in db
  QVERIFY2( QgsApplication::authManager()->masterPasswordIsSet(), "Auth master password not set from QGIS_AUTH_PASSWORD_FILE" );

  // all tests should now have a valid qgis-auth.db and stored/set master password
}

void TestQgsGdalGuiUtils::checkUriBuilding()
{
  QList<QgsAuthMethodConfig> lAuthConfigs = registerAuthConfigs();
  for ( auto &authcfg : lAuthConfigs )
  {
    QgsApplication::instance()->authManager()->storeAuthenticationConfig( authcfg, true );
  }

  // Test vsicurl: no protocol -> should default to http
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsicurl" ), QLatin1String( "www.test.com" ), QString(), QString(), QString(), false );
    QString expected( "/vsicurl/http://www.test.com" );
    QCOMPARE( actual, expected );
  }

  // Test vsicurl: with http already specified
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsicurl" ), QLatin1String( "http://www.test.com" ), QString(), QString(), QString(), false );
    // Already has protocol, just add prefix
    QString expected( "/vsicurl/http://www.test.com" );
    QCOMPARE( actual, expected );
  }

  // Test vsicurl: with https
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsicurl" ), QLatin1String( "https://data.test.com/path" ), QString(), QString(), QString(), false );
    QString expected( "/vsicurl/https://data.test.com/path" );
    QCOMPARE( actual, expected );
  }

  // Test vsicurl: with ftp
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsicurl" ), QLatin1String( "ftp://files.test.com" ), QString(), QString(), QString(), false );
    QString expected( "/vsicurl/ftp://files.test.com" );
    QCOMPARE( actual, expected );
  }

  // Test vsicurl: prefix already included
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsicurl" ), QLatin1String( "/vsicurl/http://already.prefixed.com" ), QString(), QString(), QString(), false );
    // No change expected
    QString expected( "/vsicurl/http://already.prefixed.com" );
    QCOMPARE( actual, expected );
  }

  // Test vsis3: no prefix yet
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsis3" ), QLatin1String( "bucket/key" ), QString(), QString(), QString(), false );
    QString expected( "/vsis3/bucket/key" );
    QCOMPARE( actual, expected );
  }

  // Test vsis3: prefix already included
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsis3" ), QLatin1String( "/vsis3/mydata" ), QString(), QString(), QString(), false );
    QString expected( "/vsis3/mydata" );
    QCOMPARE( actual, expected );
  }

  // Test vsigs
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsigs" ), QLatin1String( "gs://mybucket/data" ), QString(), QString(), QString(), false );
    // Even if gs:// is specified, code only prepends /vsigs/ if not present
    QString expected( "/vsigs/gs://mybucket/data" );
    QCOMPARE( actual, expected );
  }

  // Test vsiaz
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsiaz" ), QLatin1String( "mycontainer/myfile" ), QString(), QString(), QString(), false );
    QString expected( "/vsiaz/mycontainer/myfile" );
    QCOMPARE( actual, expected );
  }

  // Test vsiadls
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsiadls" ), QLatin1String( "adls_path" ), QString(), QString(), QString(), false );
    QString expected( "/vsiadls/adls_path" );
    QCOMPARE( actual, expected );
  }

  // Test vsioss
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsioss" ), QLatin1String( "my_oss_bucket/file" ), QString(), QString(), QString(), false );
    QString expected( "/vsioss/my_oss_bucket/file" );
    QCOMPARE( actual, expected );
  }

  // Test vsiswift
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsiswift" ), QLatin1String( "swift_container/object" ), QString(), QString(), QString(), false );
    QString expected( "/vsiswift/swift_container/object" );
    QCOMPARE( actual, expected );
  }

  // Test vsihdfs
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsihdfs" ), QLatin1String( "hdfs_path" ), QString(), QString(), QString(), false );
    QString expected( "/vsihdfs/hdfs_path" );
    QCOMPARE( actual, expected );
  }

  // Test GeoJSON
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "GeoJSON" ), QLatin1String( "file.json" ), QString(), QString(), QString(), false );
    // No prefix changes for GeoJSON
    QString expected( "file.json" );
    QCOMPARE( actual, expected );
  }

  // Test GeoJSONSeq
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "GeoJSONSeq" ), QLatin1String( "seq_file.json" ), QString(), QString(), QString(), false );
    // No prefix changes for GeoJSONSeq
    QString expected( "seq_file.json" );
    QCOMPARE( actual, expected );
  }

  // Test CouchDB: no prefix yet
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "CouchDB" ), QLatin1String( "localhost:5984/db" ), QString(), QString(), QString(), false );
    QString expected( "couchdb:localhost:5984/db" );
    QCOMPARE( actual, expected );
  }

  // Test CouchDB: prefix already included
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "CouchDB" ), QLatin1String( "couchdb:localhost:5984/db" ), QString(), QString(), QString(), false );
    QString expected( "couchdb:localhost:5984/db" );
    QCOMPARE( actual, expected );
  }

  // Test DODS/OPeNDAP: no prefix
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "DODS/OPeNDAP" ), QLatin1String( "http://opendap.data" ), QString(), QString(), QString(), false );
    QString expected( "DODS:http://opendap.data" );
    QCOMPARE( actual, expected );
  }

  // Test DODS/OPeNDAP: prefix already included
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "DODS/OPeNDAP" ), QLatin1String( "DODS:http://opendap.data" ), QString(), QString(), QString(), false );
    QString expected( "DODS:http://opendap.data" );
    QCOMPARE( actual, expected );
  }

  // Test WFS3: no prefix
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "WFS3" ), QLatin1String( "https://mywfs3.org" ), QString(), QString(), QString(), false );
    QString expected( "WFS3:https://mywfs3.org" );
    QCOMPARE( actual, expected );
  }

  // Test WFS3: prefix already
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "WFS3" ), QLatin1String( "WFS3:https://mywfs3.org" ), QString(), QString(), QString(), false );
    QString expected( "WFS3:https://mywfs3.org" );
    QCOMPARE( actual, expected );
  }

  // Test with configId and expandAuthConfig = false
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsicurl" ), QLatin1String( "mydata.com" ), QLatin1String( "myconfig" ), QString(), QString(), false );
    // Should prepend prefix and protocol
    QString expected( "/vsicurl/http://mydata.com authcfg='myconfig'" );
    QCOMPARE( actual, expected );
  }

  // Test with configId and no username/password, expandAuthConfig = true
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "CouchDB" ), QLatin1String( "mydb" ), QLatin1String( "ab34567" ), QString(), QString(), true );
    // Prefix is added, then authcfg updated. If no real auth expansion, it remains as is.
    // Actual code tries to expand, if fails, it returns with authcfg.
    QString expected( "couchdb:mydb" );
    QCOMPARE( actual, expected );
  }

  // Test with username/password and no configId
  {
    QString actual = QgsGdalGuiUtils::createProtocolURI( QLatin1String( "vsicurl" ), QLatin1String( "https://securedata.com" ), QString(), QLatin1String( "user" ), QLatin1String( "pass" ), false );
    // Insert credentials into the URI after "://"
    QString expected( "/vsicurl/https://user:pass@securedata.com" );
    QCOMPARE( actual, expected );
  }
}

void TestQgsGdalGuiUtils::cleanupTestCase()
{
  QgsApplication::exitQgis();
  cleanupTempDir();
}

void TestQgsGdalGuiUtils::cleanupTempDir()
{
  QString mTempDir = QDir::tempPath() + "/auth";

  QDir tmpDir = QDir( mTempDir );
  if ( tmpDir.exists() )
  {
    for ( const QString &tf : tmpDir.entryList( QDir::NoDotAndDotDot | QDir::Files ) )
    {
      QVERIFY2( tmpDir.remove( mTempDir + '/' + tf ), qPrintable( "Could not remove " + mTempDir + '/' + tf ) );
    }
    QVERIFY2( tmpDir.rmdir( mTempDir ), qPrintable( "Could not remove directory " + mTempDir ) );
  }
}

QList<QgsAuthMethodConfig> TestQgsGdalGuiUtils::registerAuthConfigs()
{
  QList<QgsAuthMethodConfig> configs;

  // Basic
  QgsAuthMethodConfig b_config;
  b_config.setName( QStringLiteral( "Basic" ) );
  b_config.setMethod( QStringLiteral( "Basic" ) );
  b_config.setUri( QStringLiteral( "http://example.com" ) );
  b_config.setConfig( QStringLiteral( "username" ), QStringLiteral( "username" ) );
  b_config.setConfig( QStringLiteral( "password" ), QStringLiteral( "password" ) );
  b_config.setConfig( QStringLiteral( "realm" ), QStringLiteral( "Realm" ) );
  b_config.setId( QStringLiteral( "ab34567" ) );
  if ( !b_config.isValid() )
  {
    return configs;
  }

  configs << b_config;
  return configs;
}

QGSTEST_MAIN( TestQgsGdalGuiUtils )
#include "testqgsgdalguiutils.moc"
