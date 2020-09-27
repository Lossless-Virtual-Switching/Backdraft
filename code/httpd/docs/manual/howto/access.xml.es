<?xml version='1.0' encoding='UTF-8' ?>
<!DOCTYPE manualpage SYSTEM "../style/manualpage.dtd">
<?xml-stylesheet type="text/xsl" href="../style/manual.es.xsl"?>
<!-- English Revision: 1745189 -->
<!-- Updated by Luis Gil de Bernabé Pfeiffer lgilbernabe[AT]apache.org -->
<!-- Reviewed by Sergio Ramos -->
<!--
 Licensed to the Apache Software Foundation (ASF) under one or more
 contributor license agreements.  See the NOTICE file distributed with
 this work for additional information regarding copyright ownership.
 The ASF licenses this file to You under the Apache License, Version 2.0
 (the "License"); you may not use this file except in compliance with
 the License.  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
-->

<manualpage metafile="access.xml.meta">
<parentdocument href="./">How-To / Tutoriales</parentdocument>

<title>Control de Acceso</title>

<summary>
    <p>El control de acceso, hace referencia a todos los medios que proporcionan
    	una forma de controlar el acceso a cualquier recurso. Esta parte está
    	separada de <a
    href="auth.html">autenticación y autorización</a>.</p>
</summary>

<section id="related"><title>Módulos y Directivas relacionados</title>

    <p>El control de acceso puede efectuarse mediante diferentes módulos. Los 
    más importantes de éstos son <module>mod_authz_core</module> y
    <module>mod_authz_host</module>. También se habla en este documento de
    el control de acceso usando el módulo <module>mod_rewrite</module>.</p>

</section>

<section id="host"><title>Control de Acceso por host</title>
    <p>
    Si lo que se quiere es restringir algunas zonas del sitio web, basándonos
    en la dirección del visitante, esto puede ser realizado de manera 
    fácil con el módulo <module>mod_authz_host</module>.
    </p>

    <p>La directiva <directive module="mod_authz_core">Require</directive>
    proporciona una variedad de diferentes maneras de permitir o denegar el acceso a los recursos. Además puede ser usada junto con las directivas:<directive
    module="mod_authz_core">RequireAll</directive>, <directive
    module="mod_authz_core">RequireAny</directive>, y <directive
    module="mod_authz_core">RequireNone</directive>, estos requerimientos pueden
    ser combinados de forma compleja y arbitraria, para cumplir cualquiera que
    sean tus políticas de acceso.</p>

    <note type="warning"><p>
    Las directivas <directive module="mod_access_compat">Allow</directive>,
    <directive module="mod_access_compat">Deny</directive>, y
    <directive module="mod_access_compat">Order</directive>,
    proporcionadas por <module>mod_access_compat</module>, están obsoletas y
    serán quitadas en futuras versiones. Deberá evitar su uso, y también
    los tutoriales desactualizaos que recomienden su uso.
    </p></note>

    <p>El uso de estas directivas es:</p>

 
    <highlight language="config">
Require host <var>address</var> <br/>
Require ip <var>ip.address</var>
    </highlight>

    <p>En la primera línea, <var>address</var> es el FQDN de un nombre de 
    dominio (o un nombre parcial del dominio); puede proporcionar múltiples
    direcciones o nombres de dominio, si se desea.
    </p>

    <p>En la segunda línea, <var>ip.address</var> es la dirección IP, una
    dirección IP parcial, una red con su máscara, o una especificación red/nnn 
    CIDR. Pueden usarse tanto IPV4 como IPV6.</p>

    <p>Consulte también <a href="../mod/mod_authz_host.html#requiredirectives">la 
    documentación de mod_authz_host </a> para otros ejemplos de esta sintaxis.
    </p>

    <p>Puede ser insertado <code>not</code> para negar un requisito en particular.
    Note que, ya que <code>not</code> es una negación de un valor, no puede ser 
    usado por si solo para permitir o denegar una petición, como <em>not true</em>
    que no contituye ser <em>false</em>. En consecuencia, para denegar una 
    visita usando una negación, el bloque debe tener un elemento que se evalúa como
    verdadero o falso. Por ejemplo, si tienes a alguien espameandote tu tablón de 
    mensajes, y tu quieres evitar que entren o dejarlos fuera, puedes realizar
    lo siguiente:
    </p>

    <highlight language="config">
&lt;RequireAll&gt;
    Require all granted
    Require not ip 10.252.46.165
&lt;/RequireAll&gt;
    </highlight>

    <p>Los visitantes que vengan desde la IP que se configura (<code>10.252.46.165</code>)
    no tendrán acceso al contenido que cubre esta directiva. Si en cambio, lo que se 
    tiene es el nombre de la máquina, en vez de la IP, podrás usar:</p>

    <highlight language="config">
Require not host <var>host.example.com</var>
    </highlight>

    <p>Y, Si lo que se quiere es bloquear el acceso desde dominio especifico, 
    	podrás especificar parte de una dirección o nombre de dominio:</p>

    <highlight language="config">
Require not ip 192.168.205
Require not host phishers.example.com moreidiots.example
Require not host gov
    </highlight>

    <p>Uso de las directivas <directive
    module="mod_authz_core">RequireAll</directive>, <directive
    module="mod_authz_core">RequireAny</directive>, y <directive
    module="mod_authz_core">RequireNone</directive> pueden ser usadas
    para forzar requisitos más complejos.</p>

</section>

<section id="env"><title>Control de acceso por variables arbitrarias.</title>

    <p>Haciendo el uso de <directive type="section" module="core">If</directive>,
    puedes permitir o denegar el acceso basado en variables de entrono arbitrarias
    o en los valores de las cabeceras de las peticiones. Por ejemplo para denegar 
    el acceso basándonos en el "user-agent" (tipo de navegador así como Sistema Operativo)
    puede que hagamos lo siguiente:
    </p>

    <highlight language="config">
&lt;If "%{HTTP_USER_AGENT} == 'BadBot'"&gt;
    Require all denied
&lt;/If&gt;
    </highlight>

    <p>Usando la sintaxis de <directive module="mod_authz_core">Require</directive>
    <code>expr</code> , esto también puede ser escrito de la siguiente forma:
    </p>


    <highlight language="config">
Require expr %{HTTP_USER_AGENT} != 'BadBot'
    </highlight>

    <note><title>Advertencia:</title>
    <p>El control de acceso por <code>User-Agent</code> es una técnica poco fiable,
    ya que la cabecera de <code>User-Agent</code> puede ser modificada y establecerse 
    al antojo del usuario.</p>
    </note>

    <p>Vea también la página de  <a href="../expr.html">expresiones</a>
    para una mayor aclaración de que sintaxis tienen las expresiones y que
    variables están disponibles.</p>

</section>

<section id="rewrite"><title>Control de acceso con mod_rewrite</title>

    <p>El flag <code>[F]</code> de <directive
    module="mod_rewrite">RewriteRule</directive> causa una respuesta 403 Forbidden
    para ser enviada. USando esto, podrá denegar el acceso a recursos basándose
    en criterio arbitrario.</p>

    <p>Por ejemplo, si lo que desea es bloquear un recurso entre las 8pm y las 
    	7am, podrá hacerlo usando <module>mod_rewrite</module>:</p>

    <highlight language="config">
RewriteEngine On
RewriteCond "%{TIME_HOUR}" "&gt;=20" [OR]
RewriteCond "%{TIME_HOUR}" "&lt;07"
RewriteRule "^/fridge"     "-"       [F]
    </highlight>

    <p>Esto devolverá una respuesta de error 403 Forbidden para cualquier  petición 
    después de las 8pm y antes de las 7am. Esta técnica puede ser usada para cualquier 
    criterio que desee usar. También puede redireccionar, o incluso reescribir estas 
    peticiones, si se prefiere ese enfoque.
    </p>

    <p>La directiva <directive type="section" module="core">If</directive>,
     añadida en la 2.4, sustituye muchas cosas que <module>mod_rewrite</module>
     tradicionalmente solía hacer, y deberá comprobar estas antes de recurrir a 
    </p>

</section>

<section id="moreinformation"><title>Más información</title>

    <p>El <a href="../expr.html">motor de expresiones</a> le da una gran
    capacidad de poder para hacer una gran variedad de cosas basadas en 
    las variables arbitrarias del servidor, y debe consultar este 
    documento para más detalles.</p>

    <p>También, deberá leer la documentación de <module>mod_authz_core</module>
    para ejemplos de combinaciones de múltiples requisitos de acceso y especificar
    cómo interactúan.
    </p>

    <p>Vea también los howtos de <a href="auth.html">Authenticación y Autorización</a>
    </p>
</section>

</manualpage>
