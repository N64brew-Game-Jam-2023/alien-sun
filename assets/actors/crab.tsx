<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.10.2" name="crab" tilewidth="48" tileheight="32" tilecount="7" columns="1">
 <image source="crab.png" width="48" height="224"/>
 <tile id="0">
  <objectgroup draworder="index" id="2">
   <object id="1" x="11" y="15" width="25" height="17"/>
   <object id="2" name="foot" x="11" y="30" width="25" height="3">
    <properties>
     <property name="sensor" type="bool" value="true"/>
    </properties>
   </object>
   <object id="4" x="8" y="12" width="31" height="6"/>
  </objectgroup>
  <animation>
   <frame tileid="0" duration="100"/>
   <frame tileid="1" duration="100"/>
   <frame tileid="2" duration="100"/>
   <frame tileid="3" duration="100"/>
   <frame tileid="4" duration="100"/>
   <frame tileid="5" duration="100"/>
   <frame tileid="6" duration="100"/>
  </animation>
 </tile>
</tileset>
