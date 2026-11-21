"""Create only the isolated release test profile, with fixture CA verification."""
import json
import os
from pathlib import Path
import grt

config=json.loads(Path(os.environ['SDP_WORKSPACE_LIVE_CONFIG']).read_text(encoding='utf-8'))
assert config['fixture']=='sdp-workspace-release-v1'
profile=Path(config['profile']).resolve(strict=True)
assert profile.name=='profile' and profile.parent==Path(config['result']).parent.resolve()
connection=grt.classes.db_mgmt_Connection()
connection.owner=grt.root.wb.rdbmsMgmt;connection.name='Workbench release fixture'
mysql=next(item for item in grt.root.wb.rdbmsMgmt.rdbms if item.name=='Mysql')
connection.driver=next(item for item in mysql.drivers if item.name=='MysqlNative')
connection.hostIdentifier='Mysql@127.0.0.1:'+str(config['port'])
# The assigned role starts inactive; connect before accessing its fixture schema.
for key,value in dict(hostName='127.0.0.1',port=config['port'],userName='ui_viewer',schema='',
                      useSSL=4,sslCA=config['ca'],timeout=20).items():
    connection.parameterValues[key]=value
grt.root.wb.rdbmsMgmt.storedConns.append(connection)
grt.serialize(grt.root.wb.rdbmsMgmt.storedConns,str(profile/'connections.xml'))
print('PASS isolated disposable TLS profile created')
