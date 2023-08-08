<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.10.2" name="meerman" tilewidth="27" tileheight="32" tilecount="2" columns="1">
 <image source="meerman.png" width="27" height="64"/>
 <tile id="0">
  <objectgroup draworder="index" id="2">
   <object id="1" x="9" y="13" width="11" height="19"/>
   <object id="2" name="foot" x="9" y="30" width="11" height="3">
    <properties>
     <property name="sensor" type="bool" value="true"/>
    </properties>
   </object>
   <object id="3" name="head" x="9" y="9" width="11" height="7">
    <properties>
     <property name="sensor" type="bool" value="true"/>
    </properties>
   </object>
  </objectgroup>
 </tile>
 <tile id="1">
  <objectgroup draworder="index" id="2">
   <object id="2" x="9" y="13" width="11" height="19"/>
   <object id="3" name="foot" x="9" y="30" width="11" height="3">
    <properties>
     <property name="sensor" type="bool" value="true"/>
    </properties>
   </object>
   <object id="4" name="head" x="9" y="9" width="11" height="7">
    <properties>
     <property name="sensor" type="bool" value="true"/>
    </properties>
   </object>
  </objectgroup>
 </tile>
</tileset>
