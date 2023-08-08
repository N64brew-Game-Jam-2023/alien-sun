<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.10.2" name="grotto" tilewidth="16" tileheight="16" tilecount="80" columns="8">
 <image source="grotto-1.png" width="128" height="160"/>
 <tile id="0">
  <properties>
   <property name="collide" type="bool" value="false"/>
  </properties>
 </tile>
 <tile id="1">
  <properties>
   <property name="collide" type="bool" value="false"/>
  </properties>
 </tile>
 <tile id="2">
  <properties>
   <property name="collide" type="bool" value="false"/>
  </properties>
 </tile>
 <tile id="3">
  <properties>
   <property name="collide" type="bool" value="false"/>
  </properties>
 </tile>
 <tile id="8">
  <properties>
   <property name="collide" type="bool" value="false"/>
  </properties>
 </tile>
 <tile id="9">
  <properties>
   <property name="drop" type="bool" value="true"/>
  </properties>
  <objectgroup draworder="index" id="2">
   <object id="2" x="0" y="0">
    <polygon points="0,0 0,4 16,8 16,4"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="10">
  <properties>
   <property name="drop" type="bool" value="true"/>
  </properties>
  <objectgroup draworder="index" id="2">
   <object id="2" x="0" y="4">
    <polygon points="0,0 0,4 16,8 16,4"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="11">
  <properties>
   <property name="drop" type="bool" value="true"/>
  </properties>
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="8" width="16" height="4">
    <properties>
     <property name="drop" type="bool" value="false"/>
    </properties>
   </object>
  </objectgroup>
 </tile>
 <tile id="31">
  <properties>
   <property name="drop" type="bool" value="true"/>
  </properties>
  <objectgroup draworder="index" id="2">
   <object id="1" x="8" y="8">
    <polygon points="0,0 -8,-8 8,-8"/>
   </object>
  </objectgroup>
  <animation>
   <frame tileid="31" duration="500"/>
   <frame tileid="39" duration="500"/>
  </animation>
 </tile>
 <tile id="34">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="12" width="16" height="4"/>
  </objectgroup>
 </tile>
 <tile id="35">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="12" width="16" height="4"/>
  </objectgroup>
 </tile>
 <tile id="39">
  <properties>
   <property name="drop" type="bool" value="true"/>
  </properties>
  <objectgroup draworder="index" id="2">
   <object id="1" x="8" y="8">
    <polygon points="0,0 -8,-8 8,-8"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="40">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0">
    <polygon points="0,0 16,16 16,0"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="49">
  <objectgroup draworder="index" id="2">
   <object id="1" x="16" y="0">
    <polygon points="0,0 -16,16 0,16"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="56">
  <objectgroup draworder="index" id="2">
   <object id="1" x="16" y="0">
    <polygon points="0,0 -16,16 0,16"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="59">
  <properties>
   <property name="drop" type="bool" value="true"/>
  </properties>
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="16" height="6"/>
  </objectgroup>
 </tile>
 <tile id="68">
  <properties>
   <property name="collide" type="bool" value="false"/>
  </properties>
 </tile>
 <tile id="69">
  <properties>
   <property name="collide" type="bool" value="false"/>
  </properties>
 </tile>
 <tile id="70">
  <properties>
   <property name="drop" type="bool" value="true"/>
  </properties>
 </tile>
 <tile id="72">
  <properties>
   <property name="collide" type="bool" value="false"/>
  </properties>
 </tile>
</tileset>
