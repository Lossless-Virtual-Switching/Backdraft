<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE modulesynopsis SYSTEM "../style/modulesynopsis.dtd">
<?xml-stylesheet type="text/xsl" href="../style/manual.es.xsl"?>
<!-- English Revision: 1741251:1879472 (outdated) -->
<!-- Translated by Luis Gil de Bernabé Pfeiffer lgilbernabe[AT]apache.org -->
<!-- Reviewed by Sergio Ramos-->
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

<modulesynopsis metafile="core.xml.meta">

<name>core</name>
<description>Funcionalides básicas del Servidor HTTP Apache que siempre están presentes.</description>
<status>Core</status>

<directivesynopsis>
<name>AcceptFilter</name>
<description>Configura mejoras para un Protocolo de Escucha de Sockets</description>
<syntax>AcceptFilter <var>protocol</var> <var>accept_filter</var></syntax>
<contextlist><context>server config</context></contextlist>


<usage>
    <p>Esta directiva hace posible mejoras específicas a nivel de sistema operativo
       y a través del tipo de Protocolo para un socket que escucha.
       La premisa básica es que el kernel no envíe un socket al servidor
       hasta que o bien los datos se hayan recibido o bien se haya almacenado
       en el buffer una Respuesta HTTP completa.  
       Actualmente sólo están soportados
       <a href="http://www.freebsd.org/cgi/man.cgi?query=accept_filter&amp;sektion=9">
       Accept Filters</a> sobre FreeBSD, <code>TCP_DEFER_ACCEPT</code> sobre Linux, 
       y AcceptEx() sobre Windows.</p>

    <p>El uso de <code>none</code> para un argumento desactiva cualquier filtro 
       aceptado para ese protocolo. Esto es útil para protocolos que requieren que un
       servidor envíe datos primeros, tales como <code>ftp:</code> o <code>nntp</code>:</p>
    <highlight language="config">
AcceptFilter nntp none
    </highlight>

    <p>Los nombres de protocolo por defecto son <code>https</code> para el puerto 443
       y <code>http</code> para todos los demás puertos. Para especificar que se está
       utilizando otro protocolo con un puerto a la escucha, añade el argumento <var>protocol</var>
       a la directiva <directive module="mpm_common">Listen</directive>.</p>

    <p>Los valores por defecto de FreeBDS son:</p>
    <highlight language="config">
AcceptFilter http httpready
AcceptFilter https dataready
    </highlight>
    
    <p>El filtro <code>httpready</code> almacena en el buffer peticiones HTTP completas
       a nivel de kernel.  Una vez que la petición es recibida, el kernel la envía al servidor. 
       Consulta la página man de
       <a href="http://www.freebsd.org/cgi/man.cgi?query=accf_http&amp;sektion=9">
       accf_http(9)</a> para más detalles.  Puesto que las peticiones HTTPS
       están encriptadas, sólo se utiliza el filtro
       <a href="http://www.freebsd.org/cgi/man.cgi?query=accf_data&amp;sektion=9">accf_data(9)</a>.</p>

    <p>Los valores por defecto en Linux son:</p>
    <highlight language="config">
AcceptFilter http data
AcceptFilter https data
    </highlight>

    <p>En Linux, <code>TCP_DEFER_ACCEPT</code> no soporta el buffering en peticiones http.
       Cualquier valor además de <code>none</code> habilitará 
       <code>TCP_DEFER_ACCEPT</code> en ese socket. Para más detalles 
       ver la página man de Linux 
       <a href="http://linux.die.net/man/7/tcp">
       tcp(7)</a>.</p>

    <p>Los valores por defecto en Windows son:</p>
    <highlight language="config">
AcceptFilter http data
AcceptFilter https data
    </highlight>

    <p>Sobre Windows mpm_winnt interpreta el argumento AcceptFilter para conmutar la API
       AcceptEx(), y no soporta el buffering sobre el protocolo http.  Hay dos valores
       que utilizan la API Windows AcceptEx() y que recuperan sockets de red
       entre conexiones.  <code>data</code> espera hasta que los datos han sido
       transmitidos como se comentaba anteriormente, y el buffer inicial de datos y las
       direcciones de red son recuperadas a partir de una única llamada AcceptEx().
       <code>connect</code> utiliza la API AcceptEx() API, y recupera también
       las direcciones de red, pero a diferencia de <code>none</code> 
       la opción <code>connect</code> no espera a la transmisión inicial de los datos.</p>

    <p>Sobre Windows, <code>none</code> usa accept() antes que AcceptEx()
       y no recuperará sockets entre las conexiones. Lo que es útil para los adaptadores de
       red con un soporte precario de drivers, así como para algunos proveedores de red
       tales como drivers vpn, o filtros de spam, de virus o de spyware.</p>  

</usage>
<seealso><directive module="core">Protocol</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>AcceptPathInfo</name>
<description>Los recursos aceptan información sobre su ruta</description>
<syntax>AcceptPathInfo On|Off|Default</syntax>
<default>AcceptPathInfo Default</default>
<contextlist><context>server config</context>
<context>virtual host</context><context>directory</context>
<context>.htaccess</context></contextlist>
<override>FileInfo</override>


<usage>

    <p>Esta directiva controla si las peticiones que contienen información sobre la ruta
    que sigue un fichero que existe (o un fichero que no existe pero en un directorio que
    sí existe) serán aceptadas o denegadas. La información de ruta puede estar disponible
    para los scripts en la variable de entorno <code>PATH_INFO</code>.</p>

    <p>Por ejemplo, asumamos que la ubicación <code>/test/</code> apunta a
    un directorio que contiene únicamente el fichero
    <code>here.html</code>. Entonces, las peticiones tanto para
    <code>/test/here.html/more</code> como para
    <code>/test/nothere.html/more</code> recogen
    <code>/more</code> como <code>PATH_INFO</code>.</p>

    <p>Los tres posibles argumentos para la directiva
    <directive>AcceptPathInfo</directive> son los siguientes:</p>
    <dl>
    <dt><code>Off</code></dt><dd>Una petición sólo será aceptada si
    se corresponde con una ruta literal que existe. Por lo tanto, una petición
    con una información de ruta después del nombre de fichero tal como
    <code>/test/here.html/more</code> en el ejemplo anterior devolverá
    un error 404 NOT FOUND.</dd>

    <dt><code>On</code></dt><dd>Una petición será aceptada si una
    ruta principal de acceso se corresponde con un fichero que existe. El ejemplo
    anterior <code>/test/here.html/more</code> será aceptado si
    <code>/test/here.html</code> corresponde a un fichero válido.</dd>

    <dt><code>Default</code></dt><dd>La gestión de las peticiones
    con información de ruta está determinada por el <a
    href="../handler.html">controlador</a> responsable de la petición.
    El controlador principal para para ficheros normales rechaza por defecto
    peticiones <code>PATH_INFO</code>. Los controladores que sirven scripts, tales como <a
    href="mod_cgi.html">cgi-script</a> e <a
    href="mod_isapi.html">isapi-handler</a>, normalmente aceptan
    <code>PATH_INFO</code> por defecto.</dd>
    </dl>

    <p>El objetivo principal de la directiva <code>AcceptPathInfo</code>
    es permitirnos sobrescribir la opción del controlador
    de aceptar o rechazar <code>PATH_INFO</code>. Este tipo de reescritura se necesita,
    por ejemplo, cuando utilizas un <a href="../filter.html">filtro</a>, tal como
    <a href="mod_include.html">INCLUDES</a>, para generar contenido
    basado en <code>PATH_INFO</code>. El controlador principal normalmente rechazaría
    la petición, de modo que puedes utilizar la siguiente configuración para habilitarla
    como script:</p>

    <highlight language="config">
&lt;Files "mypaths.shtml"&gt;
  Options +Includes
  SetOutputFilter INCLUDES
  AcceptPathInfo On
&lt;/Files&gt;
    </highlight>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>AccessFileName</name>
<description>Nombre del fichero distribuido de configuración</description>
<syntax>AccessFileName <var>filename</var> [<var>filename</var>] ...</syntax>
<default>AccessFileName .htaccess</default>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>Mientras que procesa una petición el servidor busca
    el primer fichero de configuración existente dentro de un listado de nombres en
    cada directorio de la ruta del documento, si los ficheros distribuidos
    de configuración están <a href="#allowoverride">habilitados para ese
    directorio</a>. Por ejemplo:</p>

     <highlight language="config">
AccessFileName .acl
    </highlight>

    <p>Antes de servir el documento
    <code>/usr/local/web/index.html</code>, el servidor leerá
    <code>/.acl</code>, <code>/usr/.acl</code>,
    <code>/usr/local/.acl</code> y <code>/usr/local/web/.acl</code>
    para las directivas, salvo que estén deshabilitadas con:</p>

     <highlight language="config">
&lt;Directory "/"&gt;
    AllowOverride None
&lt;/Directory&gt;
    </highlight>

    
</usage>
<seealso><directive module="core">AllowOverride</directive></seealso>
<seealso><a href="../configuring.html">Ficheros de configuración</a></seealso>
<seealso><a href="../howto/htaccess.html">Fichero .htaccess</a></seealso>
</directivesynopsis>

<directivesynopsis>
<name>AddDefaultCharset</name>
<description>Juego de casrácteres que se le añade por defecto a una respuesta del tipo
	contenido "content-type" es <code>text/plain</code> o <code>text/html</code></description>
<syntax>AddDefaultCharset On|Off|<var>charset</var></syntax>
<default>AddDefaultCharset Off</default>
<contextlist><context>server config</context>
<context>virtual host</context><context>directory</context>
<context>.htaccess</context></contextlist>
<override>FileInfo</override>

<usage>
    <p>Esta directiva especifica un valor por defecto para el tipo de soporte que 
    	se usa como parámetro del juego de carácteres (el nombre de una 
    	codificación de carácteres) para ser añadido a una respuesta si y solo si
    	el contenido de "content-type" es o <code>text/plain</code> o 
    	<code>text/html</code>. Esto debería sobreescribir cualquier juego de 
    	caracteres que se le especifique en el cuerpo de la respuesta mediante un  
    	elemento <code>META</code>, aunque el comportamiento exacto depende a menudo
    	de la confuguracion del usuario cliente. Una configuración de 
    	<code>AddDefaultCharset Off</code> deshabilita esta funcionalidad.
    	<code>AddDefaultCharset On</code> habilita un conjunto de caracteres por defecto
    	de <code>iso-8859-1</code>. Cualquier otro valor se asume que sea el <var>charset</var>
    	que va a ser usado, que debe ser uno de los juegos de carácteres  
    	<a href="http://www.iana.org/assignments/character-sets">registradas por el IANA
    </a> para su uso en los tipos de medios de Internet (MIME types).
    Por ejemplo:</p>
  	  
    <highlight language="config">
AddDefaultCharset utf-8
    </highlight>

    <p><directive>AddDefaultCharset</directive> debería ser utilizada sólo cuando
    se sepa que todo el texto del recurso al que se le aplica se sabe que va a 
    estar en ese juego de caracteres y es inconveniente etiquetar los 
    documentos individualmente. Un ejemplo de ello es añadir el juego de caracteres 
    a recursos con contenido autogenerado, tales como scripts legados de CGI, 
    que pueden ser vulnerables a ataques de tipo XSS (Cross-Site Scripting),
    debido a datos que incluye el usuario en la salida. Notese, sin embargo
    una mejor solución es arreglar (o eliminar) dichos scripts, ya que
    dejar por defecto un juego de carácteres no protege a los usuarios
    que han habilitado la funcionalidad "auto-detect character encoding" en sus 
    navegadores.
    </p>
</usage>
<seealso><directive module="mod_mime">AddCharset</directive></seealso>
</directivesynopsis> 
<directivesynopsis>
<name>AllowEncodedSlashes</name>
<description>Determina si Determines whether encoded path separators in URLs are allowed to
be passed through</description>
<syntax>AllowEncodedSlashes On|Off</syntax>
<default>AllowEncodedSlashes Off</default>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>
<compatibility>Available in Apache httpd 2.0.46 and later</compatibility>

<usage>
    <p>The <directive>AllowEncodedSlashes</directive> directive allows URLs
    which contain encoded path separators (<code>%2F</code> for <code>/</code>
    and additionally <code>%5C</code> for <code>\</code> on according systems)
    to be used. Normally such URLs are refused with a 404 (Not found) error.</p>

    <p>Turning <directive>AllowEncodedSlashes</directive> <code>On</code> is
    mostly useful when used in conjunction with <code>PATH_INFO</code>.</p>

    <note><title>Note</title>
      <p>Allowing encoded slashes does <em>not</em> imply <em>decoding</em>.
      Occurrences of <code>%2F</code> or <code>%5C</code> (<em>only</em> on
      according systems) will be left as such in the otherwise decoded URL
      string.</p>
    </note>
</usage>
<seealso><directive module="core">AcceptPathInfo</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>AllowOverride</name>
<description>Types of directives that are allowed in
<code>.htaccess</code> files</description>
<syntax>AllowOverride All|None|<var>directive-type</var>
[<var>directive-type</var>] ...</syntax>
<default>AllowOverride None (2.3.9 and later), AllowOverride All (2.3.8 and earlier)</default>
<contextlist><context>directory</context></contextlist>

<usage>
    <p>When the server finds an <code>.htaccess</code> file (as
    specified by <directive module="core">AccessFileName</directive>)
    it needs to know which directives declared in that file can override
    earlier configuration directives.</p>

    <note><title>Only available in &lt;Directory&gt; sections</title>
    <directive>AllowOverride</directive> is valid only in
    <directive type="section" module="core">Directory</directive>
    sections specified without regular expressions, not in <directive
    type="section" module="core">Location</directive>, <directive
    module="core" type="section">DirectoryMatch</directive> or
    <directive type="section" module="core">Files</directive> sections.
    </note>

    <p>When this directive is set to <code>None</code>, then
    <a href="#accessfilename">.htaccess</a> files are completely ignored.
    In this case, the server will not even attempt to read
    <code>.htaccess</code> files in the filesystem.</p>

    <p>When this directive is set to <code>All</code>, then any
    directive which has the .htaccess <a
    href="directive-dict.html#Context">Context</a> is allowed in
    <code>.htaccess</code> files.</p>

    <p>The <var>directive-type</var> can be one of the following
    groupings of directives.</p>

    <dl>
      <dt>AuthConfig</dt>

      <dd>

      Allow use of the authorization directives (<directive
      module="mod_authn_dbm">AuthDBMGroupFile</directive>,
      <directive module="mod_authn_dbm">AuthDBMUserFile</directive>,
      <directive module="mod_authz_groupfile">AuthGroupFile</directive>,
      <directive module="mod_authn_core">AuthName</directive>,
      <directive module="mod_authn_core">AuthType</directive>, <directive
      module="mod_authn_file">AuthUserFile</directive>, <directive
      module="mod_authz_core">Require</directive>, <em>etc.</em>).</dd>

      <dt>FileInfo</dt>

      <dd>
      Allow use of the directives controlling document types
     (<directive module="core">ErrorDocument</directive>,
      <directive module="core">ForceType</directive>,
      <directive module="mod_negotiation">LanguagePriority</directive>,
      <directive module="core">SetHandler</directive>,
      <directive module="core">SetInputFilter</directive>,
      <directive module="core">SetOutputFilter</directive>, and
      <module>mod_mime</module> Add* and Remove* directives),
      document meta data (<directive
      module="mod_headers">Header</directive>, <directive
      module="mod_headers">RequestHeader</directive>, <directive
      module="mod_setenvif">SetEnvIf</directive>, <directive
      module="mod_setenvif">SetEnvIfNoCase</directive>, <directive
      module="mod_setenvif">BrowserMatch</directive>, <directive
      module="mod_usertrack">CookieExpires</directive>, <directive
      module="mod_usertrack">CookieDomain</directive>, <directive
      module="mod_usertrack">CookieStyle</directive>, <directive
      module="mod_usertrack">CookieTracking</directive>, <directive
      module="mod_usertrack">CookieName</directive>),
      <module>mod_rewrite</module> directives <directive
      module="mod_rewrite">RewriteEngine</directive>, <directive
      module="mod_rewrite">RewriteOptions</directive>, <directive
      module="mod_rewrite">RewriteBase</directive>, <directive
      module="mod_rewrite">RewriteCond</directive>, <directive
      module="mod_rewrite">RewriteRule</directive>) and
      <directive module="mod_actions">Action</directive> from
      <module>mod_actions</module>.
      </dd>

      <dt>Indexes</dt>

      <dd>
      Allow use of the directives controlling directory indexing
      (<directive
      module="mod_autoindex">AddDescription</directive>,
      <directive module="mod_autoindex">AddIcon</directive>, <directive
      module="mod_autoindex">AddIconByEncoding</directive>,
      <directive module="mod_autoindex">AddIconByType</directive>,
      <directive module="mod_autoindex">DefaultIcon</directive>, <directive
      module="mod_dir">DirectoryIndex</directive>, <directive
      module="mod_autoindex">FancyIndexing</directive>, <directive
      module="mod_autoindex">HeaderName</directive>, <directive
      module="mod_autoindex">IndexIgnore</directive>, <directive
      module="mod_autoindex">IndexOptions</directive>, <directive
      module="mod_autoindex">ReadmeName</directive>,
      <em>etc.</em>).</dd>

      <dt>Limit</dt>

      <dd>
      Allow use of the directives controlling host access (<directive
      module="mod_authz_host">Allow</directive>, <directive
      module="mod_authz_host">Deny</directive> and <directive
      module="mod_authz_host">Order</directive>).</dd>

      <dt>Options[=<var>Option</var>,...]</dt>

      <dd>
      Allow use of the directives controlling specific directory
      features (<directive module="core">Options</directive> and
      <directive module="mod_include">XBitHack</directive>).
      An equal sign may be given followed by a comma (but no spaces)
      separated lists of options that may be set using the <directive
      module="core">Options</directive> command.</dd>
    </dl>

    <p>Example:</p>

    <example>
      AllowOverride AuthConfig Indexes
    </example>

    <p>In the example above all directives that are neither in the group
    <code>AuthConfig</code> nor <code>Indexes</code> cause an internal
    server error.</p>

    <note><p>For security and performance reasons, do not set
    <code>AllowOverride</code> to anything other than <code>None</code> 
    in your <code>&lt;Directory /&gt;</code> block. Instead, find (or
    create) the <code>&lt;Directory&gt;</code> block that refers to the
    directory where you're actually planning to place a
    <code>.htaccess</code> file.</p>
    </note>
</usage>

<seealso><directive module="core">AccessFileName</directive></seealso>
<seealso><a href="../configuring.html">Configuration Files</a></seealso>
<seealso><a href="../howto/htaccess.html">.htaccess Files</a></seealso>
</directivesynopsis>

<directivesynopsis>
<name>CGIMapExtension</name>
<description>Technique for locating the interpreter for CGI
scripts</description>
<syntax>CGIMapExtension <var>cgi-path</var> <var>.extension</var></syntax>
<contextlist><context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>
<compatibility>NetWare only</compatibility>

<usage>
    <p>This directive is used to control how Apache httpd finds the
    interpreter used to run CGI scripts. For example, setting
    <code>CGIMapExtension sys:\foo.nlm .foo</code> will
    cause all CGI script files with a <code>.foo</code> extension to
    be passed to the FOO interpreter.</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>ContentDigest</name>
<description>Enables the generation of <code>Content-MD5</code> HTTP Response
headers</description>
<syntax>ContentDigest On|Off</syntax>
<default>ContentDigest Off</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>Options</override>
<status>Experimental</status>

<usage>
    <p>This directive enables the generation of
    <code>Content-MD5</code> headers as defined in RFC1864
    respectively RFC2616.</p>

    <p>MD5 is an algorithm for computing a "message digest"
    (sometimes called "fingerprint") of arbitrary-length data, with
    a high degree of confidence that any alterations in the data
    will be reflected in alterations in the message digest.</p>

    <p>The <code>Content-MD5</code> header provides an end-to-end
    message integrity check (MIC) of the entity-body. A proxy or
    client may check this header for detecting accidental
    modification of the entity-body in transit. Example header:</p>

    <example>
      Content-MD5: AuLb7Dp1rqtRtxz2m9kRpA==
    </example>

    <p>Note that this can cause performance problems on your server
    since the message digest is computed on every request (the
    values are not cached).</p>

    <p><code>Content-MD5</code> is only sent for documents served
    by the <module>core</module>, and not by any module. For example,
    SSI documents, output from CGI scripts, and byte range responses
    do not have this header.</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>DefaultType</name>
<description>This directive has no effect other than to emit warnings
if the value is not <code>none</code>. In prior versions, DefaultType
would specify a default media type to assign to response content for
which no other media type configuration could be found.
</description>
<syntax>DefaultType <var>media-type|none</var></syntax>
<default>DefaultType none</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>
<compatibility>The argument <code>none</code> is available in Apache httpd 2.2.7 and later.  All other choices are DISABLED for 2.3.x and later.</compatibility>

<usage>
    <p>This directive has been disabled.  For backwards compatibility
    of configuration files, it may be specified with the value
    <code>none</code>, meaning no default media type. For example:</p>

    <example>
      DefaultType None
    </example>

    <p><code>DefaultType None</code> is only available in
    httpd-2.2.7 and later.</p>

    <p>Use the mime.types configuration file and the
    <directive module="mod_mime">AddType</directive> to configure media
    type assignments via file extensions, or the
    <directive module="core">ForceType</directive> directive to configure
    the media type for specific resources. Otherwise, the server will
    send the response without a Content-Type header field and the
    recipient may attempt to guess the media type.</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>Define</name>
<description>Define the existence of a variable</description>
<syntax>Define <var>parameter-name</var></syntax>
<contextlist><context>server config</context></contextlist>

<usage>
    <p>Equivalent to passing the <code>-D</code> argument to <program
    >httpd</program>.</p>
    <p>This directive can be used to toggle the use of <directive module="core"
    type="section">IfDefine</directive> sections without needing to alter
    <code>-D</code> arguments in any startup scripts.</p>
</usage>
</directivesynopsis>

<directivesynopsis type="section">
<name>Directory</name>
<description>Enclose a group of directives that apply only to the
named file-system directory, sub-directories, and their contents.</description>
<syntax>&lt;Directory <var>directory-path</var>&gt;
... &lt;/Directory&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p><directive type="section">Directory</directive> and
    <code>&lt;/Directory&gt;</code> are used to enclose a group of
    directives that will apply only to the named directory,
    sub-directories of that directory, and the files within the respective 
    directories.  Any directive that is allowed
    in a directory context may be used. <var>Directory-path</var> is
    either the full path to a directory, or a wild-card string using
    Unix shell-style matching. In a wild-card string, <code>?</code> matches
    any single character, and <code>*</code> matches any sequences of
    characters. You may also use <code>[]</code> character ranges. None
    of the wildcards match a `/' character, so <code>&lt;Directory
    /*/public_html&gt;</code> will not match
    <code>/home/user/public_html</code>, but <code>&lt;Directory
    /home/*/public_html&gt;</code> will match. Example:</p>

    <example>
      &lt;Directory /usr/local/httpd/htdocs&gt;<br />
      <indent>
        Options Indexes FollowSymLinks<br />
      </indent>
      &lt;/Directory&gt;
    </example>

    <note>
      <p>Be careful with the <var>directory-path</var> arguments:
      They have to literally match the filesystem path which Apache httpd uses
      to access the files. Directives applied to a particular
      <code>&lt;Directory&gt;</code> will not apply to files accessed from
      that same directory via a different path, such as via different symbolic
      links.</p>
    </note>

    <p><glossary ref="regex">Regular
    expressions</glossary> can also be used, with the addition of the
    <code>~</code> character. For example:</p>

    <example>
      &lt;Directory ~ "^/www/.*/[0-9]{3}"&gt;
    </example>

    <p>would match directories in <code>/www/</code> that consisted of
    three numbers.</p>

    <p>If multiple (non-regular expression) <directive
    type="section">Directory</directive> sections
    match the directory (or one of its parents) containing a document,
    then the directives are applied in the order of shortest match
    first, interspersed with the directives from the <a
    href="#accessfilename">.htaccess</a> files. For example,
    with</p>

    <example>
      &lt;Directory /&gt;<br />
      <indent>
        AllowOverride None<br />
      </indent>
      &lt;/Directory&gt;<br />
      <br />
      &lt;Directory /home/&gt;<br />
      <indent>
        AllowOverride FileInfo<br />
      </indent>
      &lt;/Directory&gt;
    </example>

    <p>for access to the document <code>/home/web/dir/doc.html</code>
    the steps are:</p>

    <ul>
      <li>Apply directive <code>AllowOverride None</code>
      (disabling <code>.htaccess</code> files).</li>

      <li>Apply directive <code>AllowOverride FileInfo</code> (for
      directory <code>/home</code>).</li>

      <li>Apply any <code>FileInfo</code> directives in
      <code>/home/.htaccess</code>, <code>/home/web/.htaccess</code> and
      <code>/home/web/dir/.htaccess</code> in that order.</li>
    </ul>

    <p>Regular expressions are not considered until after all of the
    normal sections have been applied. Then all of the regular
    expressions are tested in the order they appeared in the
    configuration file. For example, with</p>

    <example>
      &lt;Directory ~ abc$&gt;<br />
      <indent>
        # ... directives here ...<br />
      </indent>
      &lt;/Directory&gt;
    </example>

    <p>the regular expression section won't be considered until after
    all normal <directive type="section">Directory</directive>s and
    <code>.htaccess</code> files have been applied. Then the regular
    expression will match on <code>/home/abc/public_html/abc</code> and
    the corresponding <directive type="section">Directory</directive> will
    be applied.</p>

   <p><strong>Note that the default access for
    <code>&lt;Directory /&gt;</code> is <code>Allow from All</code>.
    This means that Apache httpd will serve any file mapped from an URL. It is
    recommended that you change this with a block such
    as</strong></p>

    <example>
      &lt;Directory /&gt;<br />
      <indent>
        Order Deny,Allow<br />
        Deny from All<br />
      </indent>
      &lt;/Directory&gt;
    </example>

    <p><strong>and then override this for directories you
    <em>want</em> accessible. See the <a
    href="../misc/security_tips.html">Security Tips</a> page for more
    details.</strong></p>

    <p>The directory sections occur in the <code>httpd.conf</code> file.
    <directive type="section">Directory</directive> directives
    cannot nest, and cannot appear in a <directive module="core"
    type="section">Limit</directive> or <directive module="core"
    type="section">LimitExcept</directive> section.</p>
</usage>
<seealso><a href="../sections.html">How &lt;Directory&gt;,
    &lt;Location&gt; and &lt;Files&gt; sections work</a> for an
    explanation of how these different sections are combined when a
    request is received</seealso>
</directivesynopsis>

<directivesynopsis type="section">
<name>DirectoryMatch</name>
<description>Enclose directives that apply to
the contents of file-system directories matching a regular expression.</description>
<syntax>&lt;DirectoryMatch <var>regex</var>&gt;
... &lt;/DirectoryMatch&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p><directive type="section">DirectoryMatch</directive> and
    <code>&lt;/DirectoryMatch&gt;</code> are used to enclose a group
    of directives which will apply only to the named directory (and the files within), 
    the same as <directive module="core" type="section">Directory</directive>. 
    However, it takes as an argument a 
    <glossary ref="regex">regular expression</glossary>.  For example:</p>

    <example>
      &lt;DirectoryMatch "^/www/(.+/)?[0-9]{3}"&gt;
    </example>

    <p>would match directories in <code>/www/</code> that consisted of three
    numbers.</p>

   <note><title>Compatability</title>
      Prior to 2.3.9, this directive implicitly applied to sub-directories
      (like <directive module="core" type="section">Directory</directive>) and
      could not match the end of line symbol ($).  In 2.3.9 and later,
      only directories that match the expression are affected by the enclosed
      directives.
    </note>

    <note><title>Trailing Slash</title>
      This directive applies to requests for directories that may or may 
      not end in a trailing slash, so expressions that are anchored to the 
      end of line ($) must be written with care.
    </note>
</usage>
<seealso><directive type="section" module="core">Directory</directive> for
a description of how regular expressions are mixed in with normal
<directive type="section">Directory</directive>s</seealso>
<seealso><a
href="../sections.html">How &lt;Directory&gt;, &lt;Location&gt; and
&lt;Files&gt; sections work</a> for an explanation of how these different
sections are combined when a request is received</seealso>
</directivesynopsis>

<directivesynopsis>
<name>DocumentRoot</name>
<description>Directory that forms the main document tree visible
from the web</description>
<syntax>DocumentRoot <var>directory-path</var></syntax>
<default>DocumentRoot /usr/local/apache/htdocs</default>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>This directive sets the directory from which <program>httpd</program>
    will serve files. Unless matched by a directive like <directive
    module="mod_alias">Alias</directive>, the server appends the
    path from the requested URL to the document root to make the
    path to the document. Example:</p>

    <example>
      DocumentRoot /usr/web
    </example>

    <p>then an access to
    <code>http://www.my.host.com/index.html</code> refers to
    <code>/usr/web/index.html</code>. If the <var>directory-path</var> is 
    not absolute then it is assumed to be relative to the <directive 
    module="core">ServerRoot</directive>.</p>

    <p>The <directive>DocumentRoot</directive> should be specified without
    a trailing slash.</p>
</usage>
<seealso><a href="../urlmapping.html#documentroot">Mapping URLs to Filesystem
Locations</a></seealso>
</directivesynopsis>

<directivesynopsis>
<name>EnableMMAP</name>
<description>Use memory-mapping to read files during delivery</description>
<syntax>EnableMMAP On|Off</syntax>
<default>EnableMMAP On</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>

<usage>
    <p>This directive controls whether the <program>httpd</program> may use
    memory-mapping if it needs to read the contents of a file during
    delivery.  By default, when the handling of a request requires
    access to the data within a file -- for example, when delivering a
    server-parsed file using <module>mod_include</module> -- Apache httpd
    memory-maps the file if the OS supports it.</p>

    <p>This memory-mapping sometimes yields a performance improvement.
    But in some environments, it is better to disable the memory-mapping
    to prevent operational problems:</p>

    <ul>
    <li>On some multiprocessor systems, memory-mapping can reduce the
    performance of the <program>httpd</program>.</li>
    <li>Deleting or truncating a file while <program>httpd</program>
      has it memory-mapped can cause <program>httpd</program> to
      crash with a segmentation fault.
    </li>
    </ul>

    <p>For server configurations that are vulnerable to these problems,
    you should disable memory-mapping of delivered files by specifying:</p>

    <example>
      EnableMMAP Off
    </example>

    <p>For NFS mounted files, this feature may be disabled explicitly for
    the offending files by specifying:</p>

    <example>
      &lt;Directory "/path-to-nfs-files"&gt;
      <indent>
        EnableMMAP Off
      </indent>
      &lt;/Directory&gt;
    </example>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>EnableSendfile</name>
<description>Use the kernel sendfile support to deliver files to the client</description>
<syntax>EnableSendfile On|Off</syntax>
<default>EnableSendfile Off</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>
<compatibility>Available in version 2.0.44 and later. Default changed to Off in
version 2.3.9.</compatibility>

<usage>
    <p>This directive controls whether <program>httpd</program> may use the
    sendfile support from the kernel to transmit file contents to the client.
    By default, when the handling of a request requires no access
    to the data within a file -- for example, when delivering a
    static file -- Apache httpd uses sendfile to deliver the file contents
    without ever reading the file if the OS supports it.</p>

    <p>This sendfile mechanism avoids separate read and send operations,
    and buffer allocations. But on some platforms or within some
    filesystems, it is better to disable this feature to avoid
    operational problems:</p>

    <ul>
    <li>Some platforms may have broken sendfile support that the build
    system did not detect, especially if the binaries were built on
    another box and moved to such a machine with broken sendfile
    support.</li>
    <li>On Linux the use of sendfile triggers TCP-checksum
    offloading bugs on certain networking cards when using IPv6.</li>
    <li>On Linux on Itanium, sendfile may be unable to handle files
    over 2GB in size.</li>
    <li>With a network-mounted <directive
    module="core">DocumentRoot</directive> (e.g., NFS, SMB, CIFS, FUSE),
    the kernel may be unable to serve the network file through
    its own cache.</li>
    </ul>

    <p>For server configurations that are not vulnerable to these problems,
    you may enable this feature by specifying:</p>

    <example>
      EnableSendfile On
    </example>

    <p>For network mounted files, this feature may be disabled explicitly
    for the offending files by specifying:</p>

    <example>
      &lt;Directory "/path-to-nfs-files"&gt;
      <indent>
        EnableSendfile Off
      </indent>
      &lt;/Directory&gt;
    </example>
    <p>Please note that the per-directory and .htaccess configuration
       of <directive>EnableSendfile</directive> is not supported by
       <module>mod_cache_disk</module>.
       Only global definition of <directive>EnableSendfile</directive>
       is taken into account by the module.
    </p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>Error</name>
<description>Abort configuration parsing with a custom error message</description>
<syntax>Error <var>message</var></syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<compatibility>2.3.9 and later</compatibility>

<usage>
    <p>If an error can be detected within the configuration, this
    directive can be used to generate a custom error message, and halt
    configuration parsing.  The typical use is for reporting required
    modules which are missing from the configuration.</p>

    <example><title>Example</title>
      # ensure that mod_include is loaded<br />
      &lt;IfModule !include_module&gt;<br />
      Error mod_include is required by mod_foo.  Load it with LoadModule.<br />
      &lt;/IfModule&gt;<br />
      <br />
      # ensure that exactly one of SSL,NOSSL is defined<br />
      &lt;IfDefine SSL&gt;<br />
      &lt;IfDefine NOSSL&gt;<br />
      Error Both SSL and NOSSL are defined.  Define only one of them.<br />
      &lt;/IfDefine&gt;<br />
      &lt;/IfDefine&gt;<br />
      &lt;IfDefine !SSL&gt;<br />
      &lt;IfDefine !NOSSL&gt;<br />
      Error Either SSL or NOSSL must be defined.<br />
      &lt;/IfDefine&gt;<br />
      &lt;/IfDefine&gt;<br />
    </example>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>ErrorDocument</name>
<description>What the server will return to the client
in case of an error</description>
<syntax>ErrorDocument <var>error-code</var> <var>document</var></syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>

<usage>
    <p>In the event of a problem or error, Apache httpd can be configured
    to do one of four things,</p>

    <ol>
      <li>output a simple hardcoded error message</li>

      <li>output a customized message</li>

      <li>redirect to a local <var>URL-path</var> to handle the
      problem/error</li>

      <li>redirect to an external <var>URL</var> to handle the
      problem/error</li>
    </ol>

    <p>The first option is the default, while options 2-4 are
    configured using the <directive>ErrorDocument</directive>
    directive, which is followed by the HTTP response code and a URL
    or a message. Apache httpd will sometimes offer additional information
    regarding the problem/error.</p>

    <p>URLs can begin with a slash (/) for local web-paths (relative
    to the <directive module="core">DocumentRoot</directive>), or be a
    full URL which the client can resolve. Alternatively, a message
    can be provided to be displayed by the browser. Examples:</p>

    <example>
      ErrorDocument 500 http://foo.example.com/cgi-bin/tester<br />
      ErrorDocument 404 /cgi-bin/bad_urls.pl<br />
      ErrorDocument 401 /subscription_info.html<br />
      ErrorDocument 403 "Sorry can't allow you access today"
    </example>

    <p>Additionally, the special value <code>default</code> can be used
    to specify Apache httpd's simple hardcoded message.  While not required
    under normal circumstances, <code>default</code> will restore
    Apache httpd's simple hardcoded message for configurations that would
    otherwise inherit an existing <directive>ErrorDocument</directive>.</p>

    <example>
      ErrorDocument 404 /cgi-bin/bad_urls.pl<br /><br />
      &lt;Directory /web/docs&gt;<br />
      <indent>
        ErrorDocument 404 default<br />
      </indent>
      &lt;/Directory&gt;
    </example>

    <p>Note that when you specify an <directive>ErrorDocument</directive>
    that points to a remote URL (ie. anything with a method such as
    <code>http</code> in front of it), Apache HTTP Server will send a redirect to the
    client to tell it where to find the document, even if the
    document ends up being on the same server. This has several
    implications, the most important being that the client will not
    receive the original error status code, but instead will
    receive a redirect status code. This in turn can confuse web
    robots and other clients which try to determine if a URL is
    valid using the status code. In addition, if you use a remote
    URL in an <code>ErrorDocument 401</code>, the client will not
    know to prompt the user for a password since it will not
    receive the 401 status code. Therefore, <strong>if you use an
    <code>ErrorDocument 401</code> directive then it must refer to a local
    document.</strong></p>

    <p>Microsoft Internet Explorer (MSIE) will by default ignore
    server-generated error messages when they are "too small" and substitute
    its own "friendly" error messages. The size threshold varies depending on
    the type of error, but in general, if you make your error document
    greater than 512 bytes, then MSIE will show the server-generated
    error rather than masking it.  More information is available in
    Microsoft Knowledge Base article <a
    href="http://support.microsoft.com/default.aspx?scid=kb;en-us;Q294807"
    >Q294807</a>.</p>

    <p>Although most error messages can be overriden, there are certain
    circumstances where the internal messages are used regardless of the
    setting of <directive module="core">ErrorDocument</directive>.  In
    particular, if a malformed request is detected, normal request processing
    will be immediately halted and the internal error message returned.
    This is necessary to guard against security problems caused by
    bad requests.</p>
   
    <p>If you are using mod_proxy, you may wish to enable
    <directive module="mod_proxy">ProxyErrorOverride</directive> so that you can provide
    custom error messages on behalf of your Origin servers. If you don't enable ProxyErrorOverride,
    Apache httpd will not generate custom error documents for proxied content.</p>
</usage>

<seealso><a href="../custom-error.html">documentation of
    customizable responses</a></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ErrorLog</name>
<description>Location where the server will log errors</description>
<syntax> ErrorLog <var>file-path</var>|syslog[:<var>facility</var>]</syntax>
<default>ErrorLog logs/error_log (Unix) ErrorLog logs/error.log (Windows and OS/2)</default>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>The <directive>ErrorLog</directive> directive sets the name of
    the file to which the server will log any errors it encounters. If
    the <var>file-path</var> is not absolute then it is assumed to be 
    relative to the <directive module="core">ServerRoot</directive>.</p>

    <example><title>Example</title>
    ErrorLog /var/log/httpd/error_log
    </example>

    <p>If the <var>file-path</var>
    begins with a pipe character "<code>|</code>" then it is assumed to be a
    command to spawn to handle the error log.</p>

    <example><title>Example</title>
    ErrorLog "|/usr/local/bin/httpd_errors"
    </example>

    <p>See the notes on <a href="../logs.html#piped">piped logs</a> for
    more information.</p>

    <p>Using <code>syslog</code> instead of a filename enables logging
    via syslogd(8) if the system supports it. The default is to use
    syslog facility <code>local7</code>, but you can override this by
    using the <code>syslog:<var>facility</var></code> syntax where
    <var>facility</var> can be one of the names usually documented in
    syslog(1).  The facility is effectively global, and if it is changed
    in individual virtual hosts, the final facility specified affects the
    entire server.</p>

    <example><title>Example</title>
    ErrorLog syslog:user
    </example>

    <p>SECURITY: See the <a
    href="../misc/security_tips.html#serverroot">security tips</a>
    document for details on why your security could be compromised
    if the directory where log files are stored is writable by
    anyone other than the user that starts the server.</p>
    <note type="warning"><title>Note</title>
      <p>When entering a file path on non-Unix platforms, care should be taken
      to make sure that only forward slashed are used even though the platform
      may allow the use of back slashes. In general it is a good idea to always 
      use forward slashes throughout the configuration files.</p>
    </note>
</usage>
<seealso><directive module="core">LogLevel</directive></seealso>
<seealso><a href="../logs.html">Apache HTTP Server Log Files</a></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ErrorLogFormat</name>
<description>Format specification for error log entries</description>
<syntax> ErrorLog [connection|request] <var>format</var></syntax>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>
<compatibility>Available in Apache httpd 2.3.9 and later</compatibility>

<usage>
    <p><directive>ErrorLogFormat</directive> allows to specify what
    supplementary information is logged in the error log in addition to the
    actual log message.</p>

    <example><title>Simple example</title>
        ErrorLogFormat "[%t] [%l] [pid %P] %F: %E: [client %a] %M"
    </example>

    <p>Specifying <code>connection</code> or <code>request</code> as first
    paramter allows to specify additional formats, causing additional
    information to be logged when the first message is logged for a specific
    connection or request, respectivly. This additional information is only
    logged once per connection/request. If a connection or request is processed
    without causing any log message, the additional information is not logged
    either.</p>

    <p>It can happen that some format string items do not produce output.  For
    example, the Referer header is only present if the log message is
    associated to a request and the log message happens at a time when the
    Referer header has already been read from the client.  If no output is
    produced, the default behaviour is to delete everything from the preceeding
    space character to the next space character.  This means the log line is
    implicitly divided into fields on non-whitespace to whitespace transitions.
    If a format string item does not produce output, the whole field is
    ommitted.  For example, if the remote address <code>%a</code> in the log
    format <code>[%t] [%l] [%a] %M&nbsp;</code> is not available, the surrounding
    brackets are not logged either.  Space characters can be escaped with a
    backslash to prevent them from delimiting a field.  The combination '%&nbsp;'
    (percent space) is a zero-witdh field delimiter that does not produce any
    output.</p>

    <p>The above behaviour can be changed by adding modifiers to the format
    string item. A <code>-</code> (minus) modifier causes a minus to be logged if the
    respective item does not produce any output. In once-per-connection/request
    formats, it is also possible to use the <code>+</code> (plus) modifier. If an
    item with the plus modifier does not produce any output, the whole line is
    ommitted.</p>

    <p>A number as modifier can be used to assign a log severity level to a
    format item. The item will only be logged if the severity of the log
    message is not higher than the specified log severity level. The number can
    range from 1 (alert) over 4 (warn) and 7 (debug) to 15 (trace8).</p>

    <p>Some format string items accept additional parameters in braces.</p>

    <table border="1" style="zebra">
    <columnspec><column width=".2"/><column width=".8"/></columnspec>

    <tr><th>Format&nbsp;String</th> <th>Description</th></tr>

    <tr><td><code>%%</code></td>
        <td>The percent sign</td></tr>

    <tr><td><code>%...a</code></td>
        <td>Remote IP-address and port</td></tr>

    <tr><td><code>%...A</code></td>
        <td>Local IP-address and port</td></tr>

    <tr><td><code>%...{name}e</code></td>
        <td>Request environment variable <code>name</code></td></tr>

    <tr><td><code>%...E</code></td>
        <td>APR/OS error status code and string</td></tr>

    <tr><td><code>%...F</code></td>
        <td>Source file name and line number of the log call</td></tr>

    <tr><td><code>%...{name}i</code></td>
        <td>Request header <code>name</code></td></tr>

    <tr><td><code>%...k</code></td>
        <td>Number of keep-alive requests on this connection</td></tr>

    <tr><td><code>%...l</code></td>
        <td>Loglevel of the message</td></tr>

    <tr><td><code>%...L</code></td>
        <td>Log ID of the request</td></tr>

    <tr><td><code>%...{c}L</code></td>
        <td>Log ID of the connection</td></tr>

    <tr><td><code>%...{C}L</code></td>
        <td>Log ID of the connection if used in connection scope, empty otherwise</td></tr>

    <tr><td><code>%...m</code></td>
        <td>Name of the module logging the message</td></tr>

    <tr><td><code>%M</code></td>
        <td>The actual log message</td></tr>

    <tr><td><code>%...{name}n</code></td>
        <td>Request note <code>name</code></td></tr>

    <tr><td><code>%...P</code></td>
        <td>Process ID of current process</td></tr>

    <tr><td><code>%...T</code></td>
        <td>Thread ID of current thread</td></tr>

    <tr><td><code>%...t</code></td>
        <td>The current time</td></tr>

    <tr><td><code>%...{u}t</code></td>
        <td>The current time including micro-seconds</td></tr>

    <tr><td><code>%...{cu}t</code></td>
        <td>The current time in compact ISO 8601 format, including
            micro-seconds</td></tr>

    <tr><td><code>%...v</code></td>
        <td>The canonical <directive module="core">ServerName</directive>
            of the current server.</td></tr>

    <tr><td><code>%...V</code></td>
        <td>The server name of the server serving the request according to the
            <directive module="core" >UseCanonicalName</directive>
            setting.</td></tr>

    <tr><td><code>\&nbsp;</code> (backslash space)</td>
        <td>Non-field delimiting space</td></tr>

    <tr><td><code>%&nbsp;</code> (percent space)</td>
        <td>Field delimiter (no output)</td></tr>
    </table>

    <p>The log ID format <code>%L</code> produces a unique id for a connection
    or request. This can be used to correlate which log lines belong to the
    same connection or request, which request happens on which connection.
    A <code>%L</code> format string is also available in
    <module>mod_log_config</module>, to allow to correlate access log entries
    with error log lines. If <module>mod_unique_id</module> is loaded, its
    unique id will be used as log ID for requests.</p>

    <example><title>Example (somewhat similar to default format)</title>
        ErrorLogFormat "[%{u}t] [%-m:%l] [pid %P] %7F: %E: [client\ %a]
        %M%&nbsp;,\&nbsp;referer\&nbsp;%{Referer}i"
    </example>

    <example><title>Example (similar to the 2.2.x format)</title>
        ErrorLogFormat "[%t] [%l] %7F: %E: [client\ %a]
        %M%&nbsp;,\&nbsp;referer\&nbsp;%{Referer}i"
    </example>

    <example><title>Advanced example with request/connection log IDs</title>
        ErrorLogFormat "[%{uc}t] [%-m:%-l] [R:%L] [C:%{C}L] %7F: %E: %M"<br/>
        ErrorLogFormat request "[%{uc}t] [R:%L] Request %k on C:%{c}L pid:%P tid:%T"<br/>
        ErrorLogFormat request "[%{uc}t] [R:%L] UA:'%+{User-Agent}i'"<br/>
        ErrorLogFormat request "[%{uc}t] [R:%L] Referer:'%+{Referer}i'"<br/>
        ErrorLogFormat connection "[%{uc}t] [C:%{c}L] local\ %a remote\ %A"<br/>
    </example>

</usage>
<seealso><directive module="core">ErrorLog</directive></seealso>
<seealso><directive module="core">LogLevel</directive></seealso>
<seealso><a href="../logs.html">Apache HTTP Server Log Files</a></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ExtendedStatus</name>
<description>Keep track of extended status information for each 
request</description>
<syntax>ExtendedStatus On|Off</syntax>
<default>ExtendedStatus Off[*]</default>
<contextlist><context>server config</context></contextlist>

<usage>
    <p>This option tracks additional data per worker about the
    currently executing request, and a utilization summary; you 
    can see these variables during runtime by configuring 
    <module>mod_status</module>.  Note that other modules may
    rely on this scoreboard.</p>

    <p>This setting applies to the entire server, and cannot be
    enabled or disabled on a virtualhost-by-virtualhost basis.
    The collection of extended status information can slow down
    the server.  Also note that this setting cannot be changed
    during a graceful restart.</p>

    <note>
    <p>Note that loading <module>mod_status</module> will change 
    the default behavior to ExtendedStatus On, while other
    third party modules may do the same.  Such modules rely on
    collecting detailed information about the state of all workers.
    The default is changed by <module>mod_status</module> beginning
    with version 2.3.6; the previous default was always Off.</p>
    </note>

</usage>

</directivesynopsis>

<directivesynopsis>
<name>FileETag</name>
<description>File attributes used to create the ETag
HTTP response header for static files</description>
<syntax>FileETag <var>component</var> ...</syntax>
<default>FileETag INode MTime Size</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>

<usage>
    <p>
    The <directive>FileETag</directive> directive configures the file
    attributes that are used to create the <code>ETag</code> (entity
    tag) response header field when the document is based on a static file.
    (The <code>ETag</code> value is used in cache management to save
    network bandwidth.) The
    <directive>FileETag</directive> directive allows you to choose
    which of these -- if any -- should be used. The recognized keywords are:
    </p>

    <dl>
     <dt><strong>INode</strong></dt>
     <dd>The file's i-node number will be included in the calculation</dd>
     <dt><strong>MTime</strong></dt>
     <dd>The date and time the file was last modified will be included</dd>
     <dt><strong>Size</strong></dt>
     <dd>The number of bytes in the file will be included</dd>
     <dt><strong>All</strong></dt>
     <dd>All available fields will be used. This is equivalent to:
         <example>FileETag INode MTime Size</example></dd>
     <dt><strong>None</strong></dt>
     <dd>If a document is file-based, no <code>ETag</code> field will be
       included in the response</dd>
    </dl>

    <p>The <code>INode</code>, <code>MTime</code>, and <code>Size</code>
    keywords may be prefixed with either <code>+</code> or <code>-</code>,
    which allow changes to be made to the default setting inherited
    from a broader scope. Any keyword appearing without such a prefix
    immediately and completely cancels the inherited setting.</p>

    <p>If a directory's configuration includes
    <code>FileETag&nbsp;INode&nbsp;MTime&nbsp;Size</code>, and a
    subdirectory's includes <code>FileETag&nbsp;-INode</code>,
    the setting for that subdirectory (which will be inherited by
    any sub-subdirectories that don't override it) will be equivalent to
    <code>FileETag&nbsp;MTime&nbsp;Size</code>.</p>
    <note type="warning"><title>Warning</title>
    Do not change the default for directories or locations that have WebDAV
    enabled and use <module>mod_dav_fs</module> as a storage provider.
    <module>mod_dav_fs</module> uses <code>INode&nbsp;MTime&nbsp;Size</code>
    as a fixed format for <code>ETag</code> comparisons on conditional requests.
    These conditional requests will break if the <code>ETag</code> format is
    changed via <directive>FileETag</directive>.
    </note>
    <note><title>Server Side Includes</title>
    An ETag is not generated for responses parsed by <module>mod_include</module>, 
    since the response entity can change without a change of the INode, MTime, or Size 
    of the static file with embedded SSI directives.
    </note>

</usage>
</directivesynopsis>

<directivesynopsis type="section">
<name>Files</name>
<description>Contains directives that apply to matched
filenames</description>
<syntax>&lt;Files <var>filename</var>&gt; ... &lt;/Files&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>All</override>

<usage>
    <p>The <directive type="section">Files</directive> directive
    limits the scope of the enclosed directives by filename. It is comparable
    to the <directive module="core" type="section">Directory</directive>
    and <directive module="core" type="section">Location</directive>
    directives. It should be matched with a <code>&lt;/Files&gt;</code>
    directive. The directives given within this section will be applied to
    any object with a basename (last component of filename) matching the
    specified filename. <directive type="section">Files</directive>
    sections are processed in the order they appear in the
    configuration file, after the <directive module="core"
    type="section">Directory</directive> sections and
    <code>.htaccess</code> files are read, but before <directive
    type="section" module="core">Location</directive> sections. Note
    that <directive type="section">Files</directive> can be nested
    inside <directive type="section"
    module="core">Directory</directive> sections to restrict the
    portion of the filesystem they apply to.</p>

    <p>The <var>filename</var> argument should include a filename, or
    a wild-card string, where <code>?</code> matches any single character,
    and <code>*</code> matches any sequences of characters.
    <glossary ref="regex">Regular expressions</glossary> 
    can also be used, with the addition of the
    <code>~</code> character. For example:</p>

    <example>
      &lt;Files ~ "\.(gif|jpe?g|png)$"&gt;
    </example>

    <p>would match most common Internet graphics formats. <directive
    module="core" type="section">FilesMatch</directive> is preferred,
    however.</p>

    <p>Note that unlike <directive type="section"
    module="core">Directory</directive> and <directive type="section"
    module="core">Location</directive> sections, <directive
    type="section">Files</directive> sections can be used inside
    <code>.htaccess</code> files. This allows users to control access to
    their own files, at a file-by-file level.</p>

</usage>
<seealso><a href="../sections.html">How &lt;Directory&gt;, &lt;Location&gt;
    and &lt;Files&gt; sections work</a> for an explanation of how these
    different sections are combined when a request is received</seealso>
</directivesynopsis>

<directivesynopsis type="section">
<name>FilesMatch</name>
<description>Contains directives that apply to regular-expression matched
filenames</description>
<syntax>&lt;FilesMatch <var>regex</var>&gt; ... &lt;/FilesMatch&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>All</override>

<usage>
    <p>The <directive type="section">FilesMatch</directive> directive
    limits the scope of the enclosed directives by filename, just as the
    <directive module="core" type="section">Files</directive> directive
    does. However, it accepts a <glossary ref="regex">regular 
    expression</glossary>. For example:</p>

    <example>
      &lt;FilesMatch "\.(gif|jpe?g|png)$"&gt;
    </example>

    <p>would match most common Internet graphics formats.</p>
</usage>

<seealso><a href="../sections.html">How &lt;Directory&gt;, &lt;Location&gt;
    and &lt;Files&gt; sections work</a> for an explanation of how these
    different sections are combined when a request is received</seealso>
</directivesynopsis>

<directivesynopsis>
<name>ForceType</name>
<description>Forces all matching files to be served with the specified
media type in the HTTP Content-Type header field</description>
<syntax>ForceType <var>media-type</var>|None</syntax>
<contextlist><context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>
<compatibility>Moved to the core in Apache httpd 2.0</compatibility>

<usage>
    <p>When placed into an <code>.htaccess</code> file or a
    <directive type="section" module="core">Directory</directive>, or
    <directive type="section" module="core">Location</directive> or
    <directive type="section" module="core">Files</directive>
    section, this directive forces all matching files to be served
    with the content type identification given by
    <var>media-type</var>. For example, if you had a directory full of
    GIF files, but did not want to label them all with <code>.gif</code>,
    you might want to use:</p>

    <example>
      ForceType image/gif
    </example>

    <p>Note that this directive overrides other indirect media type
    associations defined in mime.types or via the
    <directive module="mod_mime">AddType</directive>.</p>

    <p>You can also override more general
    <directive>ForceType</directive> settings
    by using the value of <code>None</code>:</p>

    <example>
      # force all files to be image/gif:<br />
      &lt;Location /images&gt;<br />
        <indent>
          ForceType image/gif<br />
        </indent>
      &lt;/Location&gt;<br />
      <br />
      # but normal mime-type associations here:<br />
      &lt;Location /images/mixed&gt;<br />
      <indent>
        ForceType None<br />
      </indent>
      &lt;/Location&gt;
    </example>

    <p>This directive primarily overrides the content types generated for
    static files served out of the filesystem.  For resources other than 
    static files, where the generator of the response typically specifies 
    a Content-Type, this directive has no effect.</p>

</usage>
</directivesynopsis>
<directivesynopsis>
<name>GprofDir</name>
<description>Directory to write gmon.out profiling data to.  </description>
<syntax>GprofDir <var>/tmp/gprof/</var>|<var>/tmp/gprof/</var>%</syntax>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>When the server has been compiled with gprof profiling support,
    <directive>GprofDir</directive> causes <code>gmon.out</code> files to
    be written to the specified directory when the process exits.  If the
    argument ends with a percent symbol ('%'), subdirectories are created
    for each process id.</p>

    <p>This directive currently only works with the <module>prefork</module> 
    MPM.</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>HostnameLookups</name>
<description>Enables DNS lookups on client IP addresses</description>
<syntax>HostnameLookups On|Off|Double</syntax>
<default>HostnameLookups Off</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context></contextlist>

<usage>
    <p>This directive enables DNS lookups so that host names can be
    logged (and passed to CGIs/SSIs in <code>REMOTE_HOST</code>).
    The value <code>Double</code> refers to doing double-reverse
    DNS lookup. That is, after a reverse lookup is performed, a forward
    lookup is then performed on that result. At least one of the IP
    addresses in the forward lookup must match the original
    address. (In "tcpwrappers" terminology this is called
    <code>PARANOID</code>.)</p>

    <p>Regardless of the setting, when <module>mod_authz_host</module> is
    used for controlling access by hostname, a double reverse lookup
    will be performed.  This is necessary for security. Note that the
    result of this double-reverse isn't generally available unless you
    set <code>HostnameLookups Double</code>. For example, if only
    <code>HostnameLookups On</code> and a request is made to an object
    that is protected by hostname restrictions, regardless of whether
    the double-reverse fails or not, CGIs will still be passed the
    single-reverse result in <code>REMOTE_HOST</code>.</p>

    <p>The default is <code>Off</code> in order to save the network
    traffic for those sites that don't truly need the reverse
    lookups done. It is also better for the end users because they
    don't have to suffer the extra latency that a lookup entails.
    Heavily loaded sites should leave this directive
    <code>Off</code>, since DNS lookups can take considerable
    amounts of time. The utility <program>logresolve</program>, compiled by
    default to the <code>bin</code> subdirectory of your installation
    directory, can be used to look up host names from logged IP addresses
    offline.</p>
</usage>
</directivesynopsis>

<directivesynopsis type="section">
<name>If</name>
<description>Contains directives that apply only if a condition is
satisfied by a request at runtime</description>
<syntax>&lt;If <var>expression</var>&gt; ... &lt;/If&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>All</override>

<usage>
    <p>The <directive type="section">If</directive> directive
    evaluates an expression at runtime, and applies the enclosed
    directives if and only if the expression evaluates to true.
    For example:</p>

    <example>
        &lt;If "$req{Host} = ''"&gt;
    </example>

    <p>would match HTTP/1.0 requests without a <var>Host:</var> header.</p>

    <p>You may compare the value of any variable in the request headers
    ($req), response headers ($resp) or environment ($env) in your
    expression.</p>

    <p>Apart from <code>=</code>, <code>If</code> can use the <code>IN</code>
    operator to compare if the expression is in a given range:</p>

    <example>
        &lt;If %{REQUEST_METHOD} IN GET,HEAD,OPTIONS&gt;
    </example>

</usage>

<seealso><a href="../expr.html">Expressions in Apache HTTP Server</a>,
for a complete reference and more examples.</seealso>
<seealso><a href="../sections.html">How &lt;Directory&gt;, &lt;Location&gt;,
    &lt;Files&gt; sections work</a> for an explanation of how these
    different sections are combined when a request is received.
    <directive type="section">If</directive> has the same precedence
    and usage as <directive type="section">Files</directive></seealso>
</directivesynopsis>

<directivesynopsis type="section">
<name>IfDefine</name>
<description>Encloses directives that will be processed only
if a test is true at startup</description>
<syntax>&lt;IfDefine [!]<var>parameter-name</var>&gt; ...
    &lt;/IfDefine&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>All</override>

<usage>
    <p>The <code>&lt;IfDefine <var>test</var>&gt;...&lt;/IfDefine&gt;
    </code> section is used to mark directives that are conditional. The
    directives within an <directive type="section">IfDefine</directive>
    section are only processed if the <var>test</var> is true. If <var>
    test</var> is false, everything between the start and end markers is
    ignored.</p>

    <p>The <var>test</var> in the <directive type="section"
    >IfDefine</directive> section directive can be one of two forms:</p>

    <ul>
      <li><var>parameter-name</var></li>

      <li><code>!</code><var>parameter-name</var></li>
    </ul>

    <p>In the former case, the directives between the start and end
    markers are only processed if the parameter named
    <var>parameter-name</var> is defined. The second format reverses
    the test, and only processes the directives if
    <var>parameter-name</var> is <strong>not</strong> defined.</p>

    <p>The <var>parameter-name</var> argument is a define as given on the
    <program>httpd</program> command line via <code>-D<var>parameter</var>
    </code> at the time the server was started or by the <directive
    module="core">Define</directive> directive.</p>

    <p><directive type="section">IfDefine</directive> sections are
    nest-able, which can be used to implement simple
    multiple-parameter tests. Example:</p>

    <example>
      httpd -DReverseProxy -DUseCache -DMemCache ...<br />
      <br />
      # httpd.conf<br />
      &lt;IfDefine ReverseProxy&gt;<br />
      <indent>
        LoadModule proxy_module   modules/mod_proxy.so<br />
        LoadModule proxy_http_module   modules/mod_proxy_http.so<br />
        &lt;IfDefine UseCache&gt;<br />
        <indent>
          LoadModule cache_module   modules/mod_cache.so<br />
          &lt;IfDefine MemCache&gt;<br />
          <indent>
            LoadModule mem_cache_module   modules/mod_mem_cache.so<br />
          </indent>
          &lt;/IfDefine&gt;<br />
          &lt;IfDefine !MemCache&gt;<br />
          <indent>
            LoadModule cache_disk_module   modules/mod_cache_disk.so<br />
          </indent>
          &lt;/IfDefine&gt;
        </indent>
        &lt;/IfDefine&gt;
      </indent>
      &lt;/IfDefine&gt;
    </example>
</usage>
</directivesynopsis>

<directivesynopsis type="section">
<name>IfModule</name>
<description>Encloses directives that are processed conditional on the
presence or absence of a specific module</description>
<syntax>&lt;IfModule [!]<var>module-file</var>|<var>module-identifier</var>&gt; ...
    &lt;/IfModule&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>All</override>
<compatibility>Module identifiers are available in version 2.1 and
later.</compatibility>

<usage>
    <p>The <code>&lt;IfModule <var>test</var>&gt;...&lt;/IfModule&gt;</code>
    section is used to mark directives that are conditional on the presence of
    a specific module. The directives within an <directive type="section"
    >IfModule</directive> section are only processed if the <var>test</var>
    is true. If <var>test</var> is false, everything between the start and
    end markers is ignored.</p>

    <p>The <var>test</var> in the <directive type="section"
    >IfModule</directive> section directive can be one of two forms:</p>

    <ul>
      <li><var>module</var></li>

      <li>!<var>module</var></li>
    </ul>

    <p>In the former case, the directives between the start and end
    markers are only processed if the module named <var>module</var>
    is included in Apache httpd -- either compiled in or
    dynamically loaded using <directive module="mod_so"
    >LoadModule</directive>. The second format reverses the test,
    and only processes the directives if <var>module</var> is
    <strong>not</strong> included.</p>

    <p>The <var>module</var> argument can be either the module identifier or
    the file name of the module, at the time it was compiled.  For example,
    <code>rewrite_module</code> is the identifier and
    <code>mod_rewrite.c</code> is the file name. If a module consists of
    several source files, use the name of the file containing the string
    <code>STANDARD20_MODULE_STUFF</code>.</p>

    <p><directive type="section">IfModule</directive> sections are
    nest-able, which can be used to implement simple multiple-module
    tests.</p>

    <note>This section should only be used if you need to have one
    configuration file that works whether or not a specific module
    is available. In normal operation, directives need not be
    placed in <directive type="section">IfModule</directive>
    sections.</note>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>Include</name>
<description>Includes other configuration files from within
the server configuration files</description>
<syntax>Include [<var>optional</var>|<var>strict</var>] <var>file-path</var>|<var>directory-path</var>|<var>wildcard</var></syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context>
</contextlist>
<compatibility>Wildcard matching available in 2.0.41 and later, directory
wildcard matching available in 2.3.6 and later</compatibility>

<usage>
    <p>This directive allows inclusion of other configuration files
    from within the server configuration files.</p>

    <p>Shell-style (<code>fnmatch()</code>) wildcard characters can be used
    in the filename or directory parts of the path to include several files
    at once, in alphabetical order. In addition, if
    <directive>Include</directive> points to a directory, rather than a file,
    Apache httpd will read all files in that directory and any subdirectory.
    However, including entire directories is not recommended, because it is
    easy to accidentally leave temporary files in a directory that can cause
    <program>httpd</program> to fail. Instead, we encourage you to use the
    wildcard syntax shown below, to include files that match a particular
    pattern, such as *.conf, for example.</p>

    <p>When a wildcard is specified for a <strong>file</strong> component of
    the path, and no file matches the wildcard, the
    <directive module="core">Include</directive>
    directive will be <strong>silently ignored</strong>. When a wildcard is
    specified for a <strong>directory</strong> component of the path, and
    no directory matches the wildcard, the
    <directive module="core">Include</directive> directive will
    <strong>fail with an error</strong> saying the directory cannot be found.
    </p>

    <p>For further control over the behaviour of the server when no files or
    directories match, prefix the path with the modifiers <var>optional</var>
    or <var>strict</var>. If <var>optional</var> is specified, any wildcard
    file or directory that does not match will be silently ignored. If
    <var>strict</var> is specified, any wildcard file or directory that does
    not match at least one file will cause server startup to fail.</p>

    <p>When a directory or file component of the path is
    specified exactly, and that directory or file does not exist,
    <directive module="core">Include</directive> directive will fail with an
    error saying the file or directory cannot be found.</p>

    <p>The file path specified may be an absolute path, or may be relative 
    to the <directive module="core">ServerRoot</directive> directory.</p>

    <p>Examples:</p>

    <example>
      Include /usr/local/apache2/conf/ssl.conf<br />
      Include /usr/local/apache2/conf/vhosts/*.conf
    </example>

    <p>Or, providing paths relative to your <directive
    module="core">ServerRoot</directive> directory:</p>

    <example>
      Include conf/ssl.conf<br />
      Include conf/vhosts/*.conf
    </example>

    <p>Wildcards may be included in the directory or file portion of the
    path. In the following example, the server will fail to load if no
    directories match conf/vhosts/*, but will load successfully if no
    files match *.conf.</p>
  
    <example>
      Include conf/vhosts/*/vhost.conf<br />
      Include conf/vhosts/*/*.conf
    </example>

    <p>In this example, the server will fail to load if either
    conf/vhosts/* matches no directories, or if *.conf matches no files:</p>

    <example>
      Include strict conf/vhosts/*/*.conf
    </example>
  
    <p>In this example, the server load successfully if either conf/vhosts/*
    matches no directories, or if *.conf matches no files:</p>

    <example>
      Include optional conf/vhosts/*/*.conf
    </example>

</usage>

<seealso><program>apachectl</program></seealso>
</directivesynopsis>
  
<directivesynopsis>
<name>KeepAlive</name>
<description>Enables HTTP persistent connections</description>
<syntax>KeepAlive On|Off</syntax>
<default>KeepAlive On</default>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>The Keep-Alive extension to HTTP/1.0 and the persistent
    connection feature of HTTP/1.1 provide long-lived HTTP sessions
    which allow multiple requests to be sent over the same TCP
    connection. In some cases this has been shown to result in an
    almost 50% speedup in latency times for HTML documents with
    many images. To enable Keep-Alive connections, set
    <code>KeepAlive On</code>.</p>

    <p>For HTTP/1.0 clients, Keep-Alive connections will only be
    used if they are specifically requested by a client. In
    addition, a Keep-Alive connection with an HTTP/1.0 client can
    only be used when the length of the content is known in
    advance. This implies that dynamic content such as CGI output,
    SSI pages, and server-generated directory listings will
    generally not use Keep-Alive connections to HTTP/1.0 clients.
    For HTTP/1.1 clients, persistent connections are the default
    unless otherwise specified. If the client requests it, chunked
    encoding will be used in order to send content of unknown
    length over persistent connections.</p>

    <p>When a client uses a Keep-Alive connection it will be counted
    as a single "request" for the <directive module="mpm_common"
    >MaxConnectionsPerChild</directive> directive, regardless
    of how many requests are sent using the connection.</p>
</usage>

<seealso><directive module="core">MaxKeepAliveRequests</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>KeepAliveTimeout</name>
<description>Amount of time the server will wait for subsequent
requests on a persistent connection</description>
<syntax>KeepAliveTimeout <var>num</var>[ms]</syntax>
<default>KeepAliveTimeout 5</default>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>
<compatibility>Specifying a value in milliseconds is available in 
Apache httpd 2.3.2 and later</compatibility>

<usage>
    <p>The number of seconds Apache httpd will wait for a subsequent
    request before closing the connection. By adding a postfix of ms the
    timeout can be also set in milliseconds. Once a request has been
    received, the timeout value specified by the
    <directive module="core">Timeout</directive> directive applies.</p>

    <p>Setting <directive>KeepAliveTimeout</directive> to a high value
    may cause performance problems in heavily loaded servers. The
    higher the timeout, the more server processes will be kept
    occupied waiting on connections with idle clients.</p>
    
    <p>In a name-based virtual host context, the value of the first
    defined virtual host (the default host) in a set of <directive
    module="core">NameVirtualHost</directive> will be used.
    The other values will be ignored.</p>
</usage>
</directivesynopsis>

<directivesynopsis type="section">
<name>Limit</name>
<description>Restrict enclosed access controls to only certain HTTP
methods</description>
<syntax>&lt;Limit <var>method</var> [<var>method</var>] ... &gt; ...
    &lt;/Limit&gt;</syntax>
<contextlist><context>directory</context><context>.htaccess</context>
</contextlist>
<override>AuthConfig, Limit</override>

<usage>
    <p>Access controls are normally effective for
    <strong>all</strong> access methods, and this is the usual
    desired behavior. <strong>In the general case, access control
    directives should not be placed within a
    <directive type="section">Limit</directive> section.</strong></p>

    <p>The purpose of the <directive type="section">Limit</directive>
    directive is to restrict the effect of the access controls to the
    nominated HTTP methods. For all other methods, the access
    restrictions that are enclosed in the <directive
    type="section">Limit</directive> bracket <strong>will have no
    effect</strong>. The following example applies the access control
    only to the methods <code>POST</code>, <code>PUT</code>, and
    <code>DELETE</code>, leaving all other methods unprotected:</p>

    <example>
      &lt;Limit POST PUT DELETE&gt;<br />
      <indent>
        Require valid-user<br />
      </indent>
      &lt;/Limit&gt;
    </example>

    <p>The method names listed can be one or more of: <code>GET</code>,
    <code>POST</code>, <code>PUT</code>, <code>DELETE</code>,
    <code>CONNECT</code>, <code>OPTIONS</code>,
    <code>PATCH</code>, <code>PROPFIND</code>, <code>PROPPATCH</code>,
    <code>MKCOL</code>, <code>COPY</code>, <code>MOVE</code>,
    <code>LOCK</code>, and <code>UNLOCK</code>. <strong>The method name is
    case-sensitive.</strong> If <code>GET</code> is used it will also
    restrict <code>HEAD</code> requests. The <code>TRACE</code> method
    cannot be limited (see <directive module="core"
    >TraceEnable</directive>).</p>

    <note type="warning">A <directive type="section"
    module="core">LimitExcept</directive> section should always be
    used in preference to a <directive type="section">Limit</directive>
    section when restricting access, since a <directive type="section"
    module="core">LimitExcept</directive> section provides protection
    against arbitrary methods.</note>

    <p>The <directive type="section">Limit</directive> and
    <directive type="section" module="core">LimitExcept</directive>
    directives may be nested.  In this case, each successive level of
    <directive type="section">Limit</directive> or <directive
    type="section" module="core">LimitExcept</directive> directives must
    further restrict the set of methods to which access controls apply.</p>

    <note type="warning">When using
    <directive type="section">Limit</directive> or
    <directive type="section">LimitExcept</directive> directives with
    the <directive module="mod_authz_core">Require</directive> directive,
    note that the first <directive module="mod_authz_core">Require</directive>
    to succeed authorizes the request, regardless of the presence of other
    <directive module="mod_authz_core">Require</directive> directives.</note>

    <p>For example, given the following configuration, all users will
    be authorized for <code>POST</code> requests, and the
    <code>Require group editors</code> directive will be ignored
    in all cases:</p>

    <example>
      &lt;LimitExcept GET&gt;
      <indent>
        Require valid-user
      </indent> 
      &lt;/LimitExcept&gt;<br />
      &lt;Limit POST&gt;
      <indent>
        Require group editors
      </indent> 
      &lt;/Limit&gt;
    </example>
</usage>
</directivesynopsis>

<directivesynopsis type="section">
<name>LimitExcept</name>
<description>Restrict access controls to all HTTP methods
except the named ones</description>
<syntax>&lt;LimitExcept <var>method</var> [<var>method</var>] ... &gt; ...
    &lt;/LimitExcept&gt;</syntax>
<contextlist><context>directory</context><context>.htaccess</context>
</contextlist>
<override>AuthConfig, Limit</override>

<usage>
    <p><directive type="section">LimitExcept</directive> and
    <code>&lt;/LimitExcept&gt;</code> are used to enclose
    a group of access control directives which will then apply to any
    HTTP access method <strong>not</strong> listed in the arguments;
    i.e., it is the opposite of a <directive type="section"
    module="core">Limit</directive> section and can be used to control
    both standard and nonstandard/unrecognized methods. See the
    documentation for <directive module="core"
    type="section">Limit</directive> for more details.</p>

    <p>For example:</p>

    <example>
      &lt;LimitExcept POST GET&gt;<br />
      <indent>
        Require valid-user<br />
      </indent>
      &lt;/LimitExcept&gt;
    </example>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>LimitInternalRecursion</name>
<description>Determine maximum number of internal redirects and nested
subrequests</description>
<syntax>LimitInternalRecursion <var>number</var> [<var>number</var>]</syntax>
<default>LimitInternalRecursion 10</default>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>
<compatibility>Available in Apache httpd 2.0.47 and later</compatibility>

<usage>
    <p>An internal redirect happens, for example, when using the <directive
    module="mod_actions">Action</directive> directive, which internally
    redirects the original request to a CGI script. A subrequest is Apache httpd's
    mechanism to find out what would happen for some URI if it were requested.
    For example, <module>mod_dir</module> uses subrequests to look for the
    files listed in the <directive module="mod_dir">DirectoryIndex</directive>
    directive.</p>

    <p><directive>LimitInternalRecursion</directive> prevents the server
    from crashing when entering an infinite loop of internal redirects or
    subrequests. Such loops are usually caused by misconfigurations.</p>

    <p>The directive stores two different limits, which are evaluated on
    per-request basis. The first <var>number</var> is the maximum number of
    internal redirects, that may follow each other. The second <var>number</var>
    determines, how deep subrequests may be nested. If you specify only one
    <var>number</var>, it will be assigned to both limits.</p>

    <example><title>Example</title>
      LimitInternalRecursion 5
    </example>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>LimitRequestBody</name>
<description>Restricts the total size of the HTTP request body sent
from the client</description>
<syntax>LimitRequestBody <var>bytes</var></syntax>
<default>LimitRequestBody 0</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>All</override>

<usage>
    <p>This directive specifies the number of <var>bytes</var> from 0
    (meaning unlimited) to 2147483647 (2GB) that are allowed in a
    request body. See the note below for the limited applicability
    to proxy requests.</p>

    <p>The <directive>LimitRequestBody</directive> directive allows
    the user to set a limit on the allowed size of an HTTP request
    message body within the context in which the directive is given
    (server, per-directory, per-file or per-location). If the client
    request exceeds that limit, the server will return an error
    response instead of servicing the request. The size of a normal
    request message body will vary greatly depending on the nature of
    the resource and the methods allowed on that resource. CGI scripts
    typically use the message body for retrieving form information.
    Implementations of the <code>PUT</code> method will require
    a value at least as large as any representation that the server
    wishes to accept for that resource.</p>

    <p>This directive gives the server administrator greater
    control over abnormal client request behavior, which may be
    useful for avoiding some forms of denial-of-service
    attacks.</p>

    <p>If, for example, you are permitting file upload to a particular
    location, and wish to limit the size of the uploaded file to 100K,
    you might use the following directive:</p>

    <example>
      LimitRequestBody 102400
    </example>
    
    <note><p>For a full description of how this directive is interpreted by 
    proxy requests, see the <module>mod_proxy</module> documentation.</p>
    </note>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>LimitRequestFields</name>
<description>Limits the number of HTTP request header fields that
will be accepted from the client</description>
<syntax>LimitRequestFields <var>number</var></syntax>
<default>LimitRequestFields 100</default>
<contextlist><context>server config</context><context>virtual host</context></contextlist>

<usage>
    <p><var>Number</var> is an integer from 0 (meaning unlimited) to
    32767. The default value is defined by the compile-time
    constant <code>DEFAULT_LIMIT_REQUEST_FIELDS</code> (100 as
    distributed).</p>

    <p>The <directive>LimitRequestFields</directive> directive allows
    the server administrator to modify the limit on the number of
    request header fields allowed in an HTTP request. A server needs
    this value to be larger than the number of fields that a normal
    client request might include. The number of request header fields
    used by a client rarely exceeds 20, but this may vary among
    different client implementations, often depending upon the extent
    to which a user has configured their browser to support detailed
    content negotiation. Optional HTTP extensions are often expressed
    using request header fields.</p>

    <p>This directive gives the server administrator greater
    control over abnormal client request behavior, which may be
    useful for avoiding some forms of denial-of-service attacks.
    The value should be increased if normal clients see an error
    response from the server that indicates too many fields were
    sent in the request.</p>

    <p>For example:</p>

    <example>
      LimitRequestFields 50
    </example>

     <note type="warning"><title>Warning</title>
     <p> When name-based virtual hosting is used, the value for this 
     directive is taken from the default (first-listed) virtual host for the
     <directive>NameVirtualHost</directive> the connection was mapped to.</p>
     </note>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>LimitRequestFieldSize</name>
<description>Limits the size of the HTTP request header allowed from the
client</description>
<syntax>LimitRequestFieldSize <var>bytes</var></syntax>
<default>LimitRequestFieldSize 8190</default>
<contextlist><context>server config</context><context>virtual host</context></contextlist>

<usage>
    <p>This directive specifies the number of <var>bytes</var>
    that will be allowed in an HTTP request header.</p>

    <p>The <directive>LimitRequestFieldSize</directive> directive
    allows the server administrator to reduce or increase the limit 
    on the allowed size of an HTTP request header field. A server
    needs this value to be large enough to hold any one header field 
    from a normal client request. The size of a normal request header 
    field will vary greatly among different client implementations, 
    often depending upon the extent to which a user has configured
    their browser to support detailed content negotiation. SPNEGO
    authentication headers can be up to 12392 bytes.</p>

    <p>This directive gives the server administrator greater
    control over abnormal client request behavior, which may be
    useful for avoiding some forms of denial-of-service attacks.</p>

    <p>For example:</p>

    <example>
      LimitRequestFieldSize 4094
    </example>

    <note>Under normal conditions, the value should not be changed from
    the default.</note>

    <note type="warning"><title>Warning</title>
    <p> When name-based virtual hosting is used, the value for this 
    directive is taken from the default (first-listed) virtual host for the
    <directive>NameVirtualHost</directive> the connection was mapped to.</p>
    </note>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>LimitRequestLine</name>
<description>Limit the size of the HTTP request line that will be accepted
from the client</description>
<syntax>LimitRequestLine <var>bytes</var></syntax>
<default>LimitRequestLine 8190</default>
<contextlist><context>server config</context><context>virtual host</context></contextlist>

<usage>
    <p>This directive sets the number of <var>bytes</var> that will be 
    allowed on the HTTP request-line.</p>

    <p>The <directive>LimitRequestLine</directive> directive allows
    the server administrator to reduce or increase the limit on the allowed size
    of a client's HTTP request-line. Since the request-line consists of the
    HTTP method, URI, and protocol version, the
    <directive>LimitRequestLine</directive> directive places a
    restriction on the length of a request-URI allowed for a request
    on the server. A server needs this value to be large enough to
    hold any of its resource names, including any information that
    might be passed in the query part of a <code>GET</code> request.</p>

    <p>This directive gives the server administrator greater
    control over abnormal client request behavior, which may be
    useful for avoiding some forms of denial-of-service attacks.</p>

    <p>For example:</p>

    <example>
      LimitRequestLine 4094
    </example>

    <note>Under normal conditions, the value should not be changed from
    the default.</note>

    <note type="warning"><title>Warning</title>
    <p> When name-based virtual hosting is used, the value for this 
    directive is taken from the default (first-listed) virtual host for the
    <directive>NameVirtualHost</directive> the connection was mapped to.</p>
    </note>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>LimitXMLRequestBody</name>
<description>Limits the size of an XML-based request body</description>
<syntax>LimitXMLRequestBody <var>bytes</var></syntax>
<default>LimitXMLRequestBody 1000000</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context></contextlist>
<override>All</override>

<usage>
    <p>Limit (in bytes) on maximum size of an XML-based request
    body. A value of <code>0</code> will disable any checking.</p>

    <p>Example:</p>

    <example>
      LimitXMLRequestBody 0
    </example>

</usage>
</directivesynopsis>

<directivesynopsis type="section">
<name>Location</name>
<description>Applies the enclosed directives only to matching
URLs</description>
<syntax>&lt;Location
    <var>URL-path</var>|<var>URL</var>&gt; ... &lt;/Location&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>The <directive type="section">Location</directive> directive
    limits the scope of the enclosed directives by URL. It is similar to the
    <directive type="section" module="core">Directory</directive>
    directive, and starts a subsection which is terminated with a
    <code>&lt;/Location&gt;</code> directive. <directive
    type="section">Location</directive> sections are processed in the
    order they appear in the configuration file, after the <directive
    type="section" module="core">Directory</directive> sections and
    <code>.htaccess</code> files are read, and after the <directive
    type="section" module="core">Files</directive> sections.</p>

    <p><directive type="section">Location</directive> sections operate
    completely outside the filesystem.  This has several consequences.
    Most importantly, <directive type="section">Location</directive>
    directives should not be used to control access to filesystem
    locations.  Since several different URLs may map to the same
    filesystem location, such access controls may by circumvented.</p>

    <p>The enclosed directives will be applied to the request if the path component
    of the URL meets <em>any</em> of the following criteria:
    </p>
    <ul>
      <li>The specified location matches exactly the path component of the URL.
      </li>
      <li>The specified location, which ends in a forward slash, is a prefix 
      of the path component of the URL (treated as a context root).
      </li>
      <li>The specified location, with the addition of a trailing slash, is a 
      prefix of the path component of the URL (also treated as a context root).
      </li>
    </ul>
    <p>
    In the example below, where no trailing slash is used, requests to 
    /private1, /private1/ and /private1/file.txt will have the enclosed
    directives applied, but /private1other would not. 
    </p>
    <example>
      &lt;Location /private1&gt;
          ...
    </example>
    <p>
    In the example below, where a trailing slash is used, requests to 
    /private2/ and /private2/file.txt will have the enclosed
    directives applied, but /private2 and /private2other would not. 
    </p>
    <example>
      &lt;Location /private2<em>/</em>&gt;
          ...
    </example>

    <note><title>When to use <directive 
    type="section">Location</directive></title>

    <p>Use <directive type="section">Location</directive> to apply
    directives to content that lives outside the filesystem.  For
    content that lives in the filesystem, use <directive
    type="section" module="core">Directory</directive> and <directive
    type="section" module="core">Files</directive>.  An exception is
    <code>&lt;Location /&gt;</code>, which is an easy way to 
    apply a configuration to the entire server.</p>
    </note>

    <p>For all origin (non-proxy) requests, the URL to be matched is a
    URL-path of the form <code>/path/</code>.  <em>No scheme, hostname,
    port, or query string may be included.</em>  For proxy requests, the
    URL to be matched is of the form
    <code>scheme://servername/path</code>, and you must include the
    prefix.</p>

    <p>The URL may use wildcards. In a wild-card string, <code>?</code> matches
    any single character, and <code>*</code> matches any sequences of
    characters. Neither wildcard character matches a / in the URL-path.</p>

    <p><glossary ref="regex">Regular expressions</glossary>
    can also be used, with the addition of the <code>~</code> 
    character. For example:</p>

    <example>
      &lt;Location ~ "/(extra|special)/data"&gt;
    </example>

    <p>would match URLs that contained the substring <code>/extra/data</code>
    or <code>/special/data</code>. The directive <directive
    type="section" module="core">LocationMatch</directive> behaves
    identical to the regex version of <directive
    type="section">Location</directive>, and is preferred, for the
    simple reason that <code>~</code> is hard to distinguish from
    <code>-</code> in many fonts.</p>

    <p>The <directive type="section">Location</directive>
    functionality is especially useful when combined with the
    <directive module="core">SetHandler</directive>
    directive. For example, to enable status requests, but allow them
    only from browsers at <code>example.com</code>, you might use:</p>

    <example>
      &lt;Location /status&gt;<br />
      <indent>
        SetHandler server-status<br />
        Require host example.com<br />
      </indent>
      &lt;/Location&gt;
    </example>

    <note><title>Note about / (slash)</title>
      <p>The slash character has special meaning depending on where in a
      URL it appears. People may be used to its behavior in the filesystem
      where multiple adjacent slashes are frequently collapsed to a single
      slash (<em>i.e.</em>, <code>/home///foo</code> is the same as
      <code>/home/foo</code>). In URL-space this is not necessarily true.
      The <directive type="section" module="core">LocationMatch</directive>
      directive and the regex version of <directive type="section"
      >Location</directive> require you to explicitly specify multiple
      slashes if that is your intention.</p>

      <p>For example, <code>&lt;LocationMatch ^/abc&gt;</code> would match
      the request URL <code>/abc</code> but not the request URL <code>
      //abc</code>. The (non-regex) <directive type="section"
      >Location</directive> directive behaves similarly when used for
      proxy requests. But when (non-regex) <directive type="section"
      >Location</directive> is used for non-proxy requests it will
      implicitly match multiple slashes with a single slash. For example,
      if you specify <code>&lt;Location /abc/def&gt;</code> and the
      request is to <code>/abc//def</code> then it will match.</p>
    </note>
</usage>
<seealso><a href="../sections.html">How &lt;Directory&gt;, &lt;Location&gt;
    and &lt;Files&gt; sections work</a> for an explanation of how these
    different sections are combined when a request is received.</seealso>
<seealso><directive module="core">LocationMatch</directive></seealso>
</directivesynopsis>

<directivesynopsis type="section">
<name>LocationMatch</name>
<description>Applies the enclosed directives only to regular-expression
matching URLs</description>
<syntax>&lt;LocationMatch
    <var>regex</var>&gt; ... &lt;/LocationMatch&gt;</syntax>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>The <directive type="section">LocationMatch</directive> directive
    limits the scope of the enclosed directives by URL, in an identical manner
    to <directive module="core" type="section">Location</directive>. However,
    it takes a <glossary ref="regex">regular expression</glossary>
    as an argument instead of a simple string. For example:</p>

    <example>
      &lt;LocationMatch "/(extra|special)/data"&gt;
    </example>

    <p>would match URLs that contained the substring <code>/extra/data</code>
    or <code>/special/data</code>.</p>
</usage>

<seealso><a href="../sections.html">How &lt;Directory&gt;, &lt;Location&gt;
    and &lt;Files&gt; sections work</a> for an explanation of how these
    different sections are combined when a request is received</seealso>
</directivesynopsis>

<directivesynopsis>
<name>LogLevel</name>
<description>Controls the verbosity of the ErrorLog</description>
<syntax>LogLevel [<var>module</var>:]<var>level</var>
    [<var>module</var>:<var>level</var>] ...
</syntax>
<default>LogLevel warn</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context>
</contextlist>
<compatibility>Per-module and per-directory configuration is available in
    Apache HTTP Server 2.3.6 and later</compatibility>

<usage>
    <p><directive>LogLevel</directive> adjusts the verbosity of the
    messages recorded in the error logs (see <directive
    module="core">ErrorLog</directive> directive). The following
    <var>level</var>s are available, in order of decreasing
    significance:</p>

    <table border="1">
    <columnspec><column width=".2"/><column width=".3"/><column width=".5"/>
    </columnspec>
      <tr>
        <th><strong>Level</strong> </th>

        <th><strong>Description</strong> </th>

        <th><strong>Example</strong> </th>
      </tr>

      <tr>
        <td><code>emerg</code> </td>

        <td>Emergencies - system is unusable.</td>

        <td>"Child cannot open lock file. Exiting"</td>
      </tr>

      <tr>
        <td><code>alert</code> </td>

        <td>Action must be taken immediately.</td>

        <td>"getpwuid: couldn't determine user name from uid"</td>
      </tr>

      <tr>
        <td><code>crit</code> </td>

        <td>Critical Conditions.</td>

        <td>"socket: Failed to get a socket, exiting child"</td>
      </tr>

      <tr>
        <td><code>error</code> </td>

        <td>Error conditions.</td>

        <td>"Premature end of script headers"</td>
      </tr>

      <tr>
        <td><code>warn</code> </td>

        <td>Warning conditions.</td>

        <td>"child process 1234 did not exit, sending another
        SIGHUP"</td>
      </tr>

      <tr>
        <td><code>notice</code> </td>

        <td>Normal but significant condition.</td>

        <td>"httpd: caught SIGBUS, attempting to dump core in
        ..."</td>
      </tr>

      <tr>
        <td><code>info</code> </td>

        <td>Informational.</td>

        <td>"Server seems busy, (you may need to increase
        StartServers, or Min/MaxSpareServers)..."</td>
      </tr>

      <tr>
        <td><code>debug</code> </td>

        <td>Debug-level messages</td>

        <td>"Opening config file ..."</td>
      </tr>
      <tr>
        <td><code>trace1</code> </td>

        <td>Trace messages</td>

        <td>"proxy: FTP: control connection complete"</td>
      </tr>
      <tr>
        <td><code>trace2</code> </td>

        <td>Trace messages</td>

        <td>"proxy: CONNECT: sending the CONNECT request to the remote proxy"</td>
      </tr>
      <tr>
        <td><code>trace3</code> </td>

        <td>Trace messages</td>

        <td>"openssl: Handshake: start"</td>
      </tr>
      <tr>
        <td><code>trace4</code> </td>

        <td>Trace messages</td>

        <td>"read from buffered SSL brigade, mode 0, 17 bytes"</td>
      </tr>
      <tr>
        <td><code>trace5</code> </td>

        <td>Trace messages</td>

        <td>"map lookup FAILED: map=rewritemap key=keyname"</td>
      </tr>
      <tr>
        <td><code>trace6</code> </td>

        <td>Trace messages</td>

        <td>"cache lookup FAILED, forcing new map lookup"</td>
      </tr>
      <tr>
        <td><code>trace7</code> </td>

        <td>Trace messages, dumping large amounts of data</td>

        <td>"| 0000: 02 23 44 30 13 40 ac 34 df 3d bf 9a 19 49 39 15 |"</td>
      </tr>
      <tr>
        <td><code>trace8</code> </td>

        <td>Trace messages, dumping large amounts of data</td>

        <td>"| 0000: 02 23 44 30 13 40 ac 34 df 3d bf 9a 19 49 39 15 |"</td>
      </tr>
    </table>

    <p>When a particular level is specified, messages from all
    other levels of higher significance will be reported as well.
    <em>E.g.</em>, when <code>LogLevel info</code> is specified,
    then messages with log levels of <code>notice</code> and
    <code>warn</code> will also be posted.</p>

    <p>Using a level of at least <code>crit</code> is
    recommended.</p>

    <p>For example:</p>

    <example>
      LogLevel notice
    </example>

    <note><title>Note</title>
      <p>When logging to a regular file messages of the level
      <code>notice</code> cannot be suppressed and thus are always
      logged. However, this doesn't apply when logging is done
      using <code>syslog</code>.</p>
    </note>

    <p>Specifying a level without a module name will reset the level
    for all modules to that level.  Specifying a level with a module
    name will set the level for that module only. It is possible to
    use the module source file name, the module identifier, or the
    module identifier with the trailing <code>_module</code> omitted
    as module specification. This means the following three specifications
    are equivalent:</p>

    <example>
      LogLevel info ssl:warn<br />
      LogLevel info mod_ssl.c:warn<br />
      LogLevel info ssl_module:warn<br />
    </example>

    <p>It is also possible to change the level per directory:</p>

    <example>
        LogLevel info<br />
        &lt;Directory /usr/local/apache/htdocs/app&gt;<br />
        &nbsp; LogLevel debug<br />
        &lt;/Files&gt;
    </example>

    <note>
        Per directory loglevel configuration only affects messages that are
	logged after the request has been parsed and that are associated with
	the request. Log messages which are associated with the connection or
	the server are not affected.
    </note>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>MaxKeepAliveRequests</name>
<description>Number of requests allowed on a persistent
connection</description>
<syntax>MaxKeepAliveRequests <var>number</var></syntax>
<default>MaxKeepAliveRequests 100</default>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>The <directive>MaxKeepAliveRequests</directive> directive
    limits the number of requests allowed per connection when
    <directive module="core" >KeepAlive</directive> is on. If it is
    set to <code>0</code>, unlimited requests will be allowed. We
    recommend that this setting be kept to a high value for maximum
    server performance.</p>

    <p>For example:</p>

    <example>
      MaxKeepAliveRequests 500
    </example>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>Mutex</name>
<description>Configures mutex mechanism and lock file directory for all
or specified mutexes</description>
<syntax>Mutex <var>mechanism</var> [default|<var>mutex-name</var>] ... [OmitPID]</syntax>
<default>Mutex default</default>
<contextlist><context>server config</context></contextlist>
<compatibility>Available in Apache HTTP Server 2.3.4 and later</compatibility>

<usage>
    <p>The <directive>Mutex</directive> directive sets the mechanism,
    and optionally the lock file location, that httpd and modules use
    to serialize access to resources.  Specify <code>default</code> as
    the first argument to change the settings for all mutexes; specify
    a mutex name (see table below) as the first argument to override
    defaults only for that mutex.</p>

    <p>The <directive>Mutex</directive> directive is typically used in
    the following exceptional situations:</p>

    <ul>
        <li>change the mutex mechanism when the default mechanism selected
        by <glossary>APR</glossary> has a functional or performance
        problem</li>

        <li>change the directory used by file-based mutexes when the
        default directory does not support locking</li>
    </ul>

    <note><title>Supported modules</title>
    <p>This directive only configures mutexes which have been registered
    with the core server using the <code>ap_mutex_register()</code> API.
    All modules bundled with httpd support the <directive>Mutex</directive>
    directive, but third-party modules may not.  Consult the documentation
    of the third-party module, which must indicate the mutex name(s) which
    can be configured if this directive is supported.</p>
    </note>

    <p>The following mutex <em>mechanisms</em> are available:</p>
    <ul>
        <li><code>default | yes</code>
        <p>This selects the default locking implementation, as determined by
        <glossary>APR</glossary>.  The default locking implementation can
        be displayed by running <program>httpd</program> with the 
        <code>-V</code> option.</p></li>

        <li><code>none | no</code>
        <p>This effectively disables the mutex, and is only allowed for a
        mutex if the module indicates that it is a valid choice.  Consult the
        module documentation for more information.</p></li>

        <li><code>posixsem</code>
        <p>This is a mutex variant based on a Posix semaphore.</p>

        <note type="warning"><title>Warning</title>
        <p>The semaphore ownership is not recovered if a thread in the process
        holding the mutex segfaults, resulting in a hang of the web server.</p>
        </note>
        </li>

        <li><code>sysvsem</code>
        <p>This is a mutex variant based on a SystemV IPC semaphore.</p>

        <note type="warning"><title>Warning</title>
        <p>It is possible to "leak" SysV semaphores if processes crash 
        before the semaphore is removed.</p>
	</note>

        <note type="warning"><title>Security</title>
        <p>The semaphore API allows for a denial of service attack by any
        CGIs running under the same uid as the webserver (<em>i.e.</em>,
        all CGIs, unless you use something like <program>suexec</program>
        or <code>cgiwrapper</code>).</p>
	</note>
        </li>

        <li><code>sem</code>
        <p>This selects the "best" available semaphore implementation, choosing
        between Posix and SystemV IPC semaphores, in that order.</p></li>

        <li><code>pthread</code>
        <p>This is a mutex variant based on cross-process Posix thread
        mutexes.</p>

        <note type="warning"><title>Warning</title>
        <p>On most systems, if a child process terminates abnormally while
        holding a mutex that uses this implementation, the server will deadlock
        and stop responding to requests.  When this occurs, the server will
        require a manual restart to recover.</p>
        <p>Solaris is a notable exception as it provides a mechanism which
        usually allows the mutex to be recovered after a child process
        terminates abnormally while holding a mutex.</p>
        <p>If your system implements the
        <code>pthread_mutexattr_setrobust_np()</code> function, you may be able
        to use the <code>pthread</code> option safely.</p>
        </note>
        </li>

        <li><code>fcntl:/path/to/mutex</code>
        <p>This is a mutex variant where a physical (lock-)file and the 
        <code>fcntl()</code> function are used as the mutex.</p>

        <note type="warning"><title>Warning</title>
        <p>When multiple mutexes based on this mechanism are used within
        multi-threaded, multi-process environments, deadlock errors (EDEADLK)
        can be reported for valid mutex operations if <code>fcntl()</code>
        is not thread-aware, such as on Solaris.</p>
	</note>
        </li>

        <li><code>flock:/path/to/mutex</code>
        <p>This is similar to the <code>fcntl:/path/to/mutex</code> method
        with the exception that the <code>flock()</code> function is used to
        provide file locking.</p></li>

        <li><code>file:/path/to/mutex</code>
        <p>This selects the "best" available file locking implementation,
        choosing between <code>fcntl</code> and <code>flock</code>, in that
        order.</p></li>
    </ul>

    <p>Most mechanisms are only available on selected platforms, where the 
    underlying platform and <glossary>APR</glossary> support it.  Mechanisms
    which aren't available on all platforms are <em>posixsem</em>,
    <em>sysvsem</em>, <em>sem</em>, <em>pthread</em>, <em>fcntl</em>, 
    <em>flock</em>, and <em>file</em>.</p>

    <p>With the file-based mechanisms <em>fcntl</em> and <em>flock</em>,
    the path, if provided, is a directory where the lock file will be created.
    The default directory is httpd's run-time file directory relative to
    <directive module="core">ServerRoot</directive>.  Always use a local disk
    filesystem for <code>/path/to/mutex</code> and never a directory residing
    on a NFS- or AFS-filesystem.  The basename of the file will be the mutex
    type, an optional instance string provided by the module, and unless the
    <code>OmitPID</code> keyword is specified, the process id of the httpd 
    parent process will be appended to to make the file name unique, avoiding
    conflicts when multiple httpd instances share a lock file directory.  For
    example, if the mutex name is <code>mpm-accept</code> and the lock file
    directory is <code>/var/httpd/locks</code>, the lock file name for the
    httpd instance with parent process id 12345 would be 
    <code>/var/httpd/locks/mpm-accept.12345</code>.</p>

    <note type="warning"><title>Security</title>
    <p>It is best to <em>avoid</em> putting mutex files in a world-writable
    directory such as <code>/var/tmp</code> because someone could create
    a denial of service attack and prevent the server from starting by
    creating a lockfile with the same name as the one the server will try
    to create.</p>
    </note>

    <p>The following table documents the names of mutexes used by httpd
    and bundled modules.</p>

    <table border="1" style="zebra">
        <tr>
            <th>Mutex name</th>
            <th>Module(s)</th>
            <th>Protected resource</th>
	</tr>
        <tr>
            <td><code>mpm-accept</code></td>
            <td><module>prefork</module> and <module>worker</module> MPMs</td>
            <td>incoming connections, to avoid the thundering herd problem;
            for more information, refer to the
            <a href="../misc/perf-tuning.html">performance tuning</a>
            documentation</td>
	</tr>
        <tr>
            <td><code>authdigest-client</code></td>
            <td><module>mod_auth_digest</module></td>
            <td>client list in shared memory</td>
	</tr>
        <tr>
            <td><code>authdigest-opaque</code></td>
            <td><module>mod_auth_digest</module></td>
            <td>counter in shared memory</td>
	</tr>
        <tr>
            <td><code>ldap-cache</code></td>
            <td><module>mod_ldap</module></td>
            <td>LDAP result cache</td>
	</tr>
        <tr>
            <td><code>rewrite-map</code></td>
            <td><module>mod_rewrite</module></td>
            <td>communication with external mapping programs, to avoid
            intermixed I/O from multiple requests</td>
	</tr>
        <tr>
            <td><code>ssl-cache</code></td>
            <td><module>mod_ssl</module></td>
            <td>SSL session cache</td>
	</tr>
        <tr>
            <td><code>ssl-stapling</code></td>
            <td><module>mod_ssl</module></td>
            <td>OCSP stapling response cache</td>
	</tr>
        <tr>
            <td><code>watchdog-callback</code></td>
            <td><module>mod_watchdog</module></td>
            <td>callback function of a particular client module</td>
	</tr>
    </table>

    <p>The <code>OmitPID</code> keyword suppresses the addition of the httpd
    parent process id from the lock file name.</p>

    <p>In the following example, the mutex mechanism for the MPM accept
    mutex will be changed from the compiled-in default to <code>fcntl</code>,
    with the associated lock file created in directory
    <code>/var/httpd/locks</code>.  The mutex mechanism for all other mutexes
    will be changed from the compiled-in default to <code>sysvsem</code>.</p>

    <example>
    Mutex default sysvsem<br />
    Mutex mpm-accept fcntl:/var/httpd/locks
    </example>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>NameVirtualHost</name>
<description>Designates an IP address for name-virtual
hosting</description>
<syntax>NameVirtualHost <var>addr</var>[:<var>port</var>]</syntax>
<contextlist><context>server config</context></contextlist>

<usage>

<p>A single <directive>NameVirtualHost</directive> directive 
identifies a set of identical virtual hosts on which the server will  
further select from on the basis of the <em>hostname</em> 
requested by the client.  The <directive>NameVirtualHost</directive>
directive is a required directive if you want to configure 
<a href="../vhosts/">name-based virtual hosts</a>.</p>

<p>This directive, and the corresponding <directive >VirtualHost</directive>,
<em>must</em> be qualified with a port number if the server supports both HTTP 
and HTTPS connections.</p>

<p>Although <var>addr</var> can be a hostname, it is recommended
that you always use an IP address or a wildcard.  A wildcard
NameVirtualHost matches only virtualhosts that also have a literal wildcard
as their argument.</p>

<p>In cases where a firewall or other proxy receives the requests and 
forwards them on a different IP address to the server, you must specify the
IP address of the physical interface on the machine which will be
servicing the requests. </p>

<p> In the example below, requests received on interface 192.0.2.1 and port 80 
will only select among the first two virtual hosts. Requests received on
port 80 on any other interface will only select among the third and fourth
virtual hosts. In the common case where the interface isn't important 
to the mapping, only the "*:80" NameVirtualHost and VirtualHost directives 
are necessary.</p>

   <example>
      NameVirtualHost 192.0.2.1:80<br />
      NameVirtualHost *:80<br /><br />

      &lt;VirtualHost 192.0.2.1:80&gt;<br />
      &nbsp; ServerName namebased-a.example.com<br />
      &lt;/VirtualHost&gt;<br />
      <br />
      &lt;VirtualHost 192.0.2.1:80&gt;<br />
      &nbsp; Servername namebased-b.example.com<br />
      &lt;/VirtualHost&gt;<br />
      <br />
      &lt;VirtualHost *:80&gt;<br />
      &nbsp; ServerName namebased-c.example.com <br />
      &lt;/VirtualHost&gt;<br />
      <br />
      &lt;VirtualHost *:80&gt;<br />
      &nbsp; ServerName namebased-d.example.com <br />
      &lt;/VirtualHost&gt;<br />
      <br />

    </example>

    <p>If no matching virtual host is found, then the first listed
    virtual host that matches the IP address and port will be used.</p>


    <p>IPv6 addresses must be enclosed in square brackets, as shown
    in the following example:</p>

    <example>
      NameVirtualHost [2001:db8::a00:20ff:fea7:ccea]:8080
    </example>

    <note><title>Argument to <directive type="section">VirtualHost</directive>
      directive</title>
      <p>Note that the argument to the <directive
       type="section">VirtualHost</directive> directive must
      exactly match the argument to the <directive
      >NameVirtualHost</directive> directive.</p>

      <example>
        NameVirtualHost 192.0.2.2:80<br />
        &lt;VirtualHost 192.0.2.2:80&gt;<br />
        # ...<br />
        &lt;/VirtualHost&gt;<br />
      </example>
    </note>
</usage>

<seealso><a href="../vhosts/">Virtual Hosts
documentation</a></seealso>

</directivesynopsis>

<directivesynopsis>
<name>Options</name>
<description>Configures what features are available in a particular
directory</description>
<syntax>Options
    [+|-]<var>option</var> [[+|-]<var>option</var>] ...</syntax>
<default>Options All</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>Options</override>

<usage>
    <p>The <directive>Options</directive> directive controls which
    server features are available in a particular directory.</p>

    <p><var>option</var> can be set to <code>None</code>, in which
    case none of the extra features are enabled, or one or more of
    the following:</p>

    <dl>
      <dt><code>All</code></dt>

      <dd>All options except for <code>MultiViews</code>. This is the default
      setting.</dd>

      <dt><code>ExecCGI</code></dt>

      <dd>
      Execution of CGI scripts using <module>mod_cgi</module>
      is permitted.</dd>

      <dt><code>FollowSymLinks</code></dt>

      <dd>

      The server will follow symbolic links in this directory.
      <note>
      <p>Even though the server follows the symlink it does <em>not</em>
      change the pathname used to match against <directive type="section"
      module="core">Directory</directive> sections.</p>
      <p>Note also, that this option <strong>gets ignored</strong> if set
      inside a <directive type="section" module="core">Location</directive>
      section.</p>
      <p>Omitting this option should not be considered a security restriction,
      since symlink testing is subject to race conditions that make it
      circumventable.</p>
      </note></dd>

      <dt><code>Includes</code></dt>

      <dd>
      Server-side includes provided by <module>mod_include</module>
      are permitted.</dd>

      <dt><code>IncludesNOEXEC</code></dt>

      <dd>

      Server-side includes are permitted, but the <code>#exec
      cmd</code> and <code>#exec cgi</code> are disabled. It is still
      possible to <code>#include virtual</code> CGI scripts from
      <directive module="mod_alias">ScriptAlias</directive>ed
      directories.</dd>

      <dt><code>Indexes</code></dt>

      <dd>
      If a URL which maps to a directory is requested, and there
      is no <directive module="mod_dir">DirectoryIndex</directive>
      (<em>e.g.</em>, <code>index.html</code>) in that directory, then
      <module>mod_autoindex</module> will return a formatted listing
      of the directory.</dd>

      <dt><code>MultiViews</code></dt>

      <dd>
      <a href="../content-negotiation.html">Content negotiated</a>
      "MultiViews" are allowed using
      <module>mod_negotiation</module>.
      <note><title>Note</title> <p>This option gets ignored if set
      anywhere other than <directive module="core" type="section"
      >Directory</directive>, as <module>mod_negotiation</module>
      needs real resources to compare against and evaluate from.</p></note>
      </dd>

      <dt><code>SymLinksIfOwnerMatch</code></dt>

      <dd>The server will only follow symbolic links for which the
      target file or directory is owned by the same user id as the
      link.

      <note><title>Note</title> <p>This option gets ignored if
      set inside a <directive module="core"
      type="section">Location</directive> section.</p>
      <p>This option should not be considered a security restriction,
      since symlink testing is subject to race conditions that make it
      circumventable.</p></note>
      </dd>
    </dl>

    <p>Normally, if multiple <directive>Options</directive> could
    apply to a directory, then the most specific one is used and
    others are ignored; the options are not merged. (See <a
    href="../sections.html#mergin">how sections are merged</a>.)
    However if <em>all</em> the options on the
    <directive>Options</directive> directive are preceded by a
    <code>+</code> or <code>-</code> symbol, the options are
    merged. Any options preceded by a <code>+</code> are added to the
    options currently in force, and any options preceded by a
    <code>-</code> are removed from the options currently in
    force. </p>

    <note type="warning"><title>Warning</title>
    <p>Mixing <directive>Options</directive> with a <code>+</code> or
    <code>-</code> with those without is not valid syntax, and is likely
    to cause unexpected results.</p>
    </note>

    <p>For example, without any <code>+</code> and <code>-</code> symbols:</p>

    <example>
      &lt;Directory /web/docs&gt;<br />
      <indent>
        Options Indexes FollowSymLinks<br />
      </indent>
      &lt;/Directory&gt;<br />
      <br />
      &lt;Directory /web/docs/spec&gt;<br />
      <indent>
        Options Includes<br />
      </indent>
      &lt;/Directory&gt;
    </example>

    <p>then only <code>Includes</code> will be set for the
    <code>/web/docs/spec</code> directory. However if the second
    <directive>Options</directive> directive uses the <code>+</code> and
    <code>-</code> symbols:</p>

    <example>
      &lt;Directory /web/docs&gt;<br />
      <indent>
        Options Indexes FollowSymLinks<br />
      </indent>
      &lt;/Directory&gt;<br />
      <br />
      &lt;Directory /web/docs/spec&gt;<br />
      <indent>
        Options +Includes -Indexes<br />
      </indent>
      &lt;/Directory&gt;
    </example>

    <p>then the options <code>FollowSymLinks</code> and
    <code>Includes</code> are set for the <code>/web/docs/spec</code>
    directory.</p>

    <note><title>Note</title>
      <p>Using <code>-IncludesNOEXEC</code> or
      <code>-Includes</code> disables server-side includes completely
      regardless of the previous setting.</p>
    </note>

    <p>The default in the absence of any other settings is
    <code>All</code>.</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>Protocol</name>
<description>Protocol for a listening socket</description>
<syntax>Protocol <var>protocol</var></syntax>
<contextlist><context>server config</context><context>virtual host</context></contextlist>
<compatibility>Available in Apache 2.1.5 and later.
On Windows from Apache 2.3.3 and later.</compatibility>

<usage>
    <p>This directive specifies the protocol used for a specific listening socket.
       The protocol is used to determine which module should handle a request, and
       to apply protocol specific optimizations with the <directive>AcceptFilter</directive>
       directive.</p>

    <p>You only need to set the protocol if you are running on non-standard ports, otherwise <code>http</code> is assumed for port 80 and <code>https</code> for port 443.</p>

    <p>For example, if you are running <code>https</code> on a non-standard port, specify the protocol explicitly:</p>

    <example>
      Protocol https
    </example>

    <p>You can also specify the protocol using the <directive module="mpm_common">Listen</directive> directive.</p>
</usage>
<seealso><directive>AcceptFilter</directive></seealso>
<seealso><directive module="mpm_common">Listen</directive></seealso>
</directivesynopsis>


<directivesynopsis>
<name>RLimitCPU</name>
<description>Limits the CPU consumption of processes launched
by Apache httpd children</description>
<syntax>RLimitCPU <var>seconds</var>|max [<var>seconds</var>|max]</syntax>
<default>Unset; uses operating system defaults</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context></contextlist>
<override>All</override>

<usage>
    <p>Takes 1 or 2 parameters. The first parameter sets the soft
    resource limit for all processes and the second parameter sets
    the maximum resource limit. Either parameter can be a number,
    or <code>max</code> to indicate to the server that the limit should
    be set to the maximum allowed by the operating system
    configuration. Raising the maximum resource limit requires that
    the server is running as <code>root</code>, or in the initial startup
    phase.</p>

    <p>This applies to processes forked off from Apache httpd children
    servicing requests, not the Apache httpd children themselves. This
    includes CGI scripts and SSI exec commands, but not any
    processes forked off from the Apache httpd parent such as piped
    logs.</p>

    <p>CPU resource limits are expressed in seconds per
    process.</p>
</usage>
<seealso><directive module="core">RLimitMEM</directive></seealso>
<seealso><directive module="core">RLimitNPROC</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>RLimitMEM</name>
<description>Limits the memory consumption of processes launched
by Apache httpd children</description>
<syntax>RLimitMEM <var>bytes</var>|max [<var>bytes</var>|max]</syntax>
<default>Unset; uses operating system defaults</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context></contextlist>
<override>All</override>

<usage>
    <p>Takes 1 or 2 parameters. The first parameter sets the soft
    resource limit for all processes and the second parameter sets
    the maximum resource limit. Either parameter can be a number,
    or <code>max</code> to indicate to the server that the limit should
    be set to the maximum allowed by the operating system
    configuration. Raising the maximum resource limit requires that
    the server is running as <code>root</code>, or in the initial startup
    phase.</p>

    <p>This applies to processes forked off from Apache httpd children
    servicing requests, not the Apache httpd children themselves. This
    includes CGI scripts and SSI exec commands, but not any
    processes forked off from the Apache httpd parent such as piped
    logs.</p>

    <p>Memory resource limits are expressed in bytes per
    process.</p>
</usage>
<seealso><directive module="core">RLimitCPU</directive></seealso>
<seealso><directive module="core">RLimitNPROC</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>RLimitNPROC</name>
<description>Limits the number of processes that can be launched by
processes launched by Apache httpd children</description>
<syntax>RLimitNPROC <var>number</var>|max [<var>number</var>|max]</syntax>
<default>Unset; uses operating system defaults</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context></contextlist>
<override>All</override>

<usage>
    <p>Takes 1 or 2 parameters. The first parameter sets the soft
    resource limit for all processes and the second parameter sets
    the maximum resource limit. Either parameter can be a number,
    or <code>max</code> to indicate to the server that the limit
    should be set to the maximum allowed by the operating system
    configuration. Raising the maximum resource limit requires that
    the server is running as <code>root</code>, or in the initial startup
    phase.</p>

    <p>This applies to processes forked off from Apache httpd children
    servicing requests, not the Apache httpd children themselves. This
    includes CGI scripts and SSI exec commands, but not any
    processes forked off from the Apache httpd parent such as piped
    logs.</p>

    <p>Process limits control the number of processes per user.</p>

    <note><title>Note</title>
      <p>If CGI processes are <strong>not</strong> running
      under user ids other than the web server user id, this directive
      will limit the number of processes that the server itself can
      create. Evidence of this situation will be indicated by
      <strong><code>cannot fork</code></strong> messages in the
      <code>error_log</code>.</p>
    </note>
</usage>
<seealso><directive module="core">RLimitMEM</directive></seealso>
<seealso><directive module="core">RLimitCPU</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ScriptInterpreterSource</name>
<description>Technique for locating the interpreter for CGI
scripts</description>
<syntax>ScriptInterpreterSource Registry|Registry-Strict|Script</syntax>
<default>ScriptInterpreterSource Script</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context></contextlist>
<override>FileInfo</override>
<compatibility>Win32 only;
option <code>Registry-Strict</code> is available in Apache HTTP Server 2.0 and
later</compatibility>

<usage>
    <p>This directive is used to control how Apache httpd finds the
    interpreter used to run CGI scripts. The default setting is
    <code>Script</code>. This causes Apache httpd to use the interpreter pointed to
    by the shebang line (first line, starting with <code>#!</code>) in the
    script. On Win32 systems this line usually looks like:</p>

    <example>
      #!C:/Perl/bin/perl.exe
    </example>

    <p>or, if <code>perl</code> is in the <code>PATH</code>, simply:</p>

    <example>
      #!perl
    </example>

    <p>Setting <code>ScriptInterpreterSource Registry</code> will
    cause the Windows Registry tree <code>HKEY_CLASSES_ROOT</code> to be
    searched using the script file extension (e.g., <code>.pl</code>) as a
    search key. The command defined by the registry subkey
    <code>Shell\ExecCGI\Command</code> or, if it does not exist, by the subkey
    <code>Shell\Open\Command</code> is used to open the script file. If the
    registry keys cannot be found, Apache httpd falls back to the behavior of the
    <code>Script</code> option.</p>

    <note type="warning"><title>Security</title>
    <p>Be careful when using <code>ScriptInterpreterSource
    Registry</code> with <directive
    module="mod_alias">ScriptAlias</directive>'ed directories, because
    Apache httpd will try to execute <strong>every</strong> file within this
    directory. The <code>Registry</code> setting may cause undesired
    program calls on files which are typically not executed. For
    example, the default open command on <code>.htm</code> files on
    most Windows systems will execute Microsoft Internet Explorer, so
    any HTTP request for an <code>.htm</code> file existing within the
    script directory would start the browser in the background on the
    server. This is a good way to crash your system within a minute or
    so.</p>
    </note>

    <p>The option <code>Registry-Strict</code> which is new in Apache HTTP Server
    2.0 does the same thing as <code>Registry</code> but uses only the
    subkey <code>Shell\ExecCGI\Command</code>. The
    <code>ExecCGI</code> key is not a common one. It must be
    configured manually in the windows registry and hence prevents
    accidental program calls on your system.</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>SeeRequestTail</name>
<description>Determine if mod_status displays the first 63 characters
of a request or the last 63, assuming the request itself is greater than
63 chars.</description>
<syntax>SeeRequestTail On|Off</syntax>
<default>SeeRequestTail Off</default>
<contextlist><context>server config</context></contextlist>
<compatibility>Available in Apache httpd 2.2.7 and later.</compatibility>

<usage>
    <p>mod_status with <code>ExtendedStatus On</code>
    displays the actual request being handled. 
    For historical purposes, only 63 characters of the request
    are actually stored for display purposes. This directive
    controls whether the 1st 63 characters are stored (the previous
    behavior and the default) or if the last 63 characters are. This
    is only applicable, of course, if the length of the request is
    64 characters or greater.</p>

    <p>If Apache httpd is handling <code
    >GET&nbsp;/disk1/storage/apache/htdocs/images/imagestore1/food/apples.jpg&nbsp;HTTP/1.1</code
    > mod_status displays as follows:
    </p>

    <table border="1">
      <tr>
        <th>Off (default)</th>
        <td>GET&nbsp;/disk1/storage/apache/htdocs/images/imagestore1/food/apples</td>
      </tr>
      <tr>
        <th>On</th>
        <td>orage/apache/htdocs/images/imagestore1/food/apples.jpg&nbsp;HTTP/1.1</td>
      </tr>
    </table>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>ServerAdmin</name>
<description>Email address that the server includes in error
messages sent to the client</description>
<syntax>ServerAdmin <var>email-address</var>|<var>URL</var></syntax>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>The <directive>ServerAdmin</directive> sets the contact address
    that the server includes in any error messages it returns to the
    client. If the <code>httpd</code> doesn't recognize the supplied argument
    as an URL, it
    assumes, that it's an <var>email-address</var> and prepends it with
    <code>mailto:</code> in hyperlink targets. However, it's recommended to
    actually use an email address, since there are a lot of CGI scripts that
    make that assumption. If you want to use an URL, it should point to another
    server under your control. Otherwise users may not be able to contact you in
    case of errors.</p>

    <p>It may be worth setting up a dedicated address for this, e.g.</p>

    <example>
      ServerAdmin www-admin@foo.example.com
    </example>
    <p>as users do not always mention that they are talking about the
    server!</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>ServerAlias</name>
<description>Alternate names for a host used when matching requests
to name-virtual hosts</description>
<syntax>ServerAlias <var>hostname</var> [<var>hostname</var>] ...</syntax>
<contextlist><context>virtual host</context></contextlist>

<usage>
    <p>The <directive>ServerAlias</directive> directive sets the
    alternate names for a host, for use with <a
    href="../vhosts/name-based.html">name-based virtual hosts</a>. The
    <directive>ServerAlias</directive> may include wildcards, if appropriate.</p>

    <example>
      &lt;VirtualHost *:80&gt;<br />
      ServerName server.domain.com<br />
      ServerAlias server server2.domain.com server2<br />
      ServerAlias *.example.com<br />
      UseCanonicalName Off<br />
      # ...<br />
      &lt;/VirtualHost&gt;
    </example>
</usage>
<seealso><directive module="core">UseCanonicalName</directive></seealso>
<seealso><a href="../vhosts/">Apache HTTP Server Virtual Host documentation</a></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ServerName</name>
<description>Hostname and port that the server uses to identify
itself</description>
<syntax>ServerName [<var>scheme</var>://]<var>fully-qualified-domain-name</var>[:<var>port</var>]</syntax>
<contextlist><context>server config</context><context>virtual host</context>
</contextlist>

<usage>
    <p>The <directive>ServerName</directive> directive sets the
    request scheme, hostname and
    port that the server uses to identify itself.  This is used when
    creating redirection URLs.</p>

    <p>Additionally, <directive>ServerName</directive> is used (possibly
    in conjunction with <directive>ServerAlias</directive>) to uniquely
    identify a virtual host, when using <a
    href="../vhosts/name-based.html">name-based virtual hosts</a>.</p>
    
    <p>For example, if the name of the
    machine hosting the web server is <code>simple.example.com</code>,
    but the machine also has the DNS alias <code>www.example.com</code>
    and you wish the web server to be so identified, the following
    directive should be used:</p>

    <example>
      ServerName www.example.com:80
    </example>

    <p>The <directive>ServerName</directive> directive
    may appear anywhere within the definition of a server. However,
    each appearance overrides the previous appearance (within that
    server).</p>

    <p>If no <directive>ServerName</directive> is specified, then the
    server attempts to deduce the hostname by performing a reverse
    lookup on the IP address. If no port is specified in the
    <directive>ServerName</directive>, then the server will use the
    port from the incoming request. For optimal reliability and
    predictability, you should specify an explicit hostname and port
    using the <directive>ServerName</directive> directive.</p>

    <p>If you are using <a
    href="../vhosts/name-based.html">name-based virtual hosts</a>,
    the <directive>ServerName</directive> inside a
    <directive type="section" module="core">VirtualHost</directive>
    section specifies what hostname must appear in the request's
    <code>Host:</code> header to match this virtual host.</p>

    <p>Sometimes, the server runs behind a device that processes SSL,
    such as a reverse proxy, load balancer or SSL offload
    appliance. When this is the case, specify the
    <code>https://</code> scheme and the port number to which the
    clients connect in the <directive>ServerName</directive> directive
    to make sure that the server generates the correct
    self-referential URLs. 
    </p>

    <p>See the description of the
    <directive module="core">UseCanonicalName</directive> and
    <directive module="core">UseCanonicalPhysicalPort</directive> directives for
    settings which determine whether self-referential URLs (e.g., by the
    <module>mod_dir</module> module) will refer to the
    specified port, or to the port number given in the client's request.
    </p>

    <note type="warning">
    <p>Failure to set <directive>ServerName</directive> to a name that
    your server can resolve to an IP address will result in a startup
    warning. <code>httpd</code> will then use whatever hostname it can
    determine, using the system's <code>hostname</code> command. This
    will almost never be the hostname you actually want.</p>
    <example>
    httpd: Could not reliably determine the server's fully qualified domain name, using rocinante.local for ServerName
    </example>
    </note>

</usage>

<seealso><a href="../dns-caveats.html">Issues Regarding DNS and
    Apache HTTP Server</a></seealso>
<seealso><a href="../vhosts/">Apache HTTP Server virtual host
    documentation</a></seealso>
<seealso><directive module="core">UseCanonicalName</directive></seealso>
<seealso><directive module="core">UseCanonicalPhysicalPort</directive></seealso>
<seealso><directive module="core">NameVirtualHost</directive></seealso>
<seealso><directive module="core">ServerAlias</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ServerPath</name>
<description>Legacy URL pathname for a name-based virtual host that
is accessed by an incompatible browser</description>
<syntax>ServerPath <var>URL-path</var></syntax>
<contextlist><context>virtual host</context></contextlist>

<usage>
    <p>The <directive>ServerPath</directive> directive sets the legacy
    URL pathname for a host, for use with <a
    href="../vhosts/">name-based virtual hosts</a>.</p>
</usage>
<seealso><a href="../vhosts/">Apache HTTP Server Virtual Host documentation</a></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ServerRoot</name>
<description>Base directory for the server installation</description>
<syntax>ServerRoot <var>directory-path</var></syntax>
<default>ServerRoot /usr/local/apache</default>
<contextlist><context>server config</context></contextlist>

<usage>
    <p>The <directive>ServerRoot</directive> directive sets the
    directory in which the server lives. Typically it will contain the
    subdirectories <code>conf/</code> and <code>logs/</code>. Relative
    paths in other configuration directives (such as <directive
    module="core">Include</directive> or <directive
    module="mod_so">LoadModule</directive>, for example) are taken as 
    relative to this directory.</p>

    <example><title>Example</title>
      ServerRoot /home/httpd
    </example>

</usage>
<seealso><a href="../invoking.html">the <code>-d</code>
    option to <code>httpd</code></a></seealso>
<seealso><a href="../misc/security_tips.html#serverroot">the
    security tips</a> for information on how to properly set
    permissions on the <directive>ServerRoot</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ServerSignature</name>
<description>Configures the footer on server-generated documents</description>
<syntax>ServerSignature On|Off|EMail</syntax>
<default>ServerSignature Off</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>All</override>

<usage>
    <p>The <directive>ServerSignature</directive> directive allows the
    configuration of a trailing footer line under server-generated
    documents (error messages, <module>mod_proxy</module> ftp directory
    listings, <module>mod_info</module> output, ...). The reason why you
    would want to enable such a footer line is that in a chain of proxies,
    the user often has no possibility to tell which of the chained servers
    actually produced a returned error message.</p>

    <p>The <code>Off</code>
    setting, which is the default, suppresses the footer line (and is
    therefore compatible with the behavior of Apache-1.2 and
    below). The <code>On</code> setting simply adds a line with the
    server version number and <directive
    module="core">ServerName</directive> of the serving virtual host,
    and the <code>EMail</code> setting additionally creates a
    "mailto:" reference to the <directive
    module="core">ServerAdmin</directive> of the referenced
    document.</p>

    <p>After version 2.0.44, the details of the server version number
    presented are controlled by the <directive
    module="core">ServerTokens</directive> directive.</p>
</usage>
<seealso><directive module="core">ServerTokens</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>ServerTokens</name>
<description>Configures the <code>Server</code> HTTP response
header</description>
<syntax>ServerTokens Major|Minor|Min[imal]|Prod[uctOnly]|OS|Full</syntax>
<default>ServerTokens Full</default>
<contextlist><context>server config</context></contextlist>

<usage>
    <p>This directive controls whether <code>Server</code> response
    header field which is sent back to clients includes a
    description of the generic OS-type of the server as well as
    information about compiled-in modules.</p>

    <dl>
      <dt><code>ServerTokens Full</code> (or not specified)</dt>

      <dd>Server sends (<em>e.g.</em>): <code>Server: Apache/2.4.1
      (Unix) PHP/4.2.2 MyMod/1.2</code></dd>

      <dt><code>ServerTokens Prod[uctOnly]</code></dt>

      <dd>Server sends (<em>e.g.</em>): <code>Server:
      Apache</code></dd>

      <dt><code>ServerTokens Major</code></dt>

      <dd>Server sends (<em>e.g.</em>): <code>Server:
      Apache/2</code></dd>

      <dt><code>ServerTokens Minor</code></dt>

      <dd>Server sends (<em>e.g.</em>): <code>Server:
      Apache/2.4</code></dd>

      <dt><code>ServerTokens Min[imal]</code></dt>

      <dd>Server sends (<em>e.g.</em>): <code>Server:
      Apache/2.4.1</code></dd>

      <dt><code>ServerTokens OS</code></dt>

      <dd>Server sends (<em>e.g.</em>): <code>Server: Apache/2.4.1
      (Unix)</code></dd>

    </dl>

    <p>This setting applies to the entire server, and cannot be
    enabled or disabled on a virtualhost-by-virtualhost basis.</p>

    <p>After version 2.0.44, this directive also controls the
    information presented by the <directive
    module="core">ServerSignature</directive> directive.</p>
    
    <note>Setting <directive>ServerTokens</directive> to less than
    <code>minimal</code> is not recommended because it makes it more
    difficult to debug interoperational problems. Also note that
    disabling the Server: header does nothing at all to make your
    server more secure; the idea of "security through obscurity"
    is a myth and leads to a false sense of safety.</note>

</usage>
<seealso><directive module="core">ServerSignature</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>SetHandler</name>
<description>Forces all matching files to be processed by a
handler</description>
<syntax>SetHandler <var>handler-name</var>|None</syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>
<compatibility>Moved into the core in Apache httpd 2.0</compatibility>

<usage>
    <p>When placed into an <code>.htaccess</code> file or a
    <directive type="section" module="core">Directory</directive> or
    <directive type="section" module="core">Location</directive>
    section, this directive forces all matching files to be parsed
    through the <a href="../handler.html">handler</a> given by
    <var>handler-name</var>. For example, if you had a directory you
    wanted to be parsed entirely as imagemap rule files, regardless
    of extension, you might put the following into an
    <code>.htaccess</code> file in that directory:</p>

    <example>
      SetHandler imap-file
    </example>

    <p>Another example: if you wanted to have the server display a
    status report whenever a URL of
    <code>http://servername/status</code> was called, you might put
    the following into <code>httpd.conf</code>:</p>

    <example>
      &lt;Location /status&gt;<br />
      <indent>
        SetHandler server-status<br />
      </indent>
      &lt;/Location&gt;
    </example>

    <p>You can override an earlier defined <directive>SetHandler</directive>
    directive by using the value <code>None</code>.</p>
    <p><strong>Note:</strong> because SetHandler overrides default handlers,
    normal behaviour such as handling of URLs ending in a slash (/) as
    directories or index files is suppressed.</p>
</usage>

<seealso><directive module="mod_mime">AddHandler</directive></seealso>

</directivesynopsis>

<directivesynopsis>
<name>SetInputFilter</name>
<description>Sets the filters that will process client requests and POST
input</description>
<syntax>SetInputFilter <var>filter</var>[;<var>filter</var>...]</syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>

<usage>
    <p>The <directive>SetInputFilter</directive> directive sets the
    filter or filters which will process client requests and POST
    input when they are received by the server. This is in addition to
    any filters defined elsewhere, including the
    <directive module="mod_mime">AddInputFilter</directive>
    directive.</p>

    <p>If more than one filter is specified, they must be separated
    by semicolons in the order in which they should process the
    content.</p>
</usage>
<seealso><a href="../filter.html">Filters</a> documentation</seealso>
</directivesynopsis>

<directivesynopsis>
<name>SetOutputFilter</name>
<description>Sets the filters that will process responses from the
server</description>
<syntax>SetOutputFilter <var>filter</var>[;<var>filter</var>...]</syntax>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context><context>.htaccess</context>
</contextlist>
<override>FileInfo</override>

<usage>
    <p>The <directive>SetOutputFilter</directive> directive sets the filters
    which will process responses from the server before they are
    sent to the client. This is in addition to any filters defined
    elsewhere, including the
    <directive module="mod_mime">AddOutputFilter</directive>
    directive.</p>

    <p>For example, the following configuration will process all files
    in the <code>/www/data/</code> directory for server-side
    includes.</p>

    <example>
      &lt;Directory /www/data/&gt;<br />
      <indent>
        SetOutputFilter INCLUDES<br />
      </indent>
      &lt;/Directory&gt;
    </example>

    <p>If more than one filter is specified, they must be separated
    by semicolons in the order in which they should process the
    content.</p>
</usage>
<seealso><a href="../filter.html">Filters</a> documentation</seealso>
</directivesynopsis>

<directivesynopsis>
<name>TimeOut</name>
<description>Amount of time the server will wait for
certain events before failing a request</description>
<syntax>TimeOut <var>seconds</var></syntax>
<default>TimeOut 60</default>
<contextlist><context>server config</context><context>virtual host</context></contextlist>

<usage>
    <p>The <directive>TimeOut</directive> directive defines the length
    of time Apache httpd will wait for I/O in various circumstances:</p>

    <ol>
      <li>When reading data from the client, the length of time to
      wait for a TCP packet to arrive if the read buffer is
      empty.</li>

      <li>When writing data to the client, the length of time to wait
      for an acknowledgement of a packet if the send buffer is
      full.</li>

      <li>In <module>mod_cgi</module>, the length of time to wait for
      output from a CGI script.</li>

      <li>In <module>mod_ext_filter</module>, the length of time to
      wait for output from a filtering process.</li>

      <li>In <module>mod_proxy</module>, the default timeout value if
      <directive module="mod_proxy">ProxyTimeout</directive> is not
      configured.</li>
    </ol>

</usage>
</directivesynopsis>

<directivesynopsis>
<name>TraceEnable</name>
<description>Determines the behaviour on <code>TRACE</code> requests</description>
<syntax>TraceEnable <var>[on|off|extended]</var></syntax>
<default>TraceEnable on</default>
<contextlist><context>server config</context></contextlist>
<compatibility>Available in Apache HTTP Server 1.3.34, 2.0.55 and later</compatibility>

<usage>
    <p>This directive overrides the behavior of <code>TRACE</code> for both
    the core server and <module>mod_proxy</module>.  The default
    <code>TraceEnable on</code> permits <code>TRACE</code> requests per
    RFC 2616, which disallows any request body to accompany the request.
    <code>TraceEnable off</code> causes the core server and
    <module>mod_proxy</module> to return a <code>405</code> (Method not
    allowed) error to the client.</p>

    <p>Finally, for testing and diagnostic purposes only, request
    bodies may be allowed using the non-compliant <code>TraceEnable 
    extended</code> directive.  The core (as an origin server) will
    restrict the request body to 64k (plus 8k for chunk headers if
    <code>Transfer-Encoding: chunked</code> is used).  The core will
    reflect the full headers and all chunk headers with the response
    body.  As a proxy server, the request body is not restricted to 64k.</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>UnDefine</name>
<description>Undefine the existence of a variable</description>
<syntax>UnDefine <var>parameter-name</var></syntax>
<contextlist><context>server config</context></contextlist>

<usage>
    <p>Undoes the effect of a <directive module="core">Define</directive> or
    of passing a <code>-D</code> argument to <program>httpd</program>.</p>
    <p>This directive can be used to toggle the use of <directive module="core"
    type="section">IfDefine</directive> sections without needing to alter
    <code>-D</code> arguments in any startup scripts.</p>
</usage>
</directivesynopsis>

<directivesynopsis>
<name>UseCanonicalName</name>
<description>Configures how the server determines its own name and
port</description>
<syntax>UseCanonicalName On|Off|DNS</syntax>
<default>UseCanonicalName Off</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context></contextlist>

<usage>
    <p>In many situations Apache httpd must construct a <em>self-referential</em>
    URL -- that is, a URL that refers back to the same server. With
    <code>UseCanonicalName On</code> Apache httpd will use the hostname and port
    specified in the <directive module="core">ServerName</directive>
    directive to construct the canonical name for the server. This name
    is used in all self-referential URLs, and for the values of
    <code>SERVER_NAME</code> and <code>SERVER_PORT</code> in CGIs.</p>

    <p>With <code>UseCanonicalName Off</code> Apache httpd will form
    self-referential URLs using the hostname and port supplied by
    the client if any are supplied (otherwise it will use the
    canonical name, as defined above). These values are the same
    that are used to implement <a
    href="../vhosts/name-based.html">name-based virtual hosts</a>,
    and are available with the same clients. The CGI variables
    <code>SERVER_NAME</code> and <code>SERVER_PORT</code> will be
    constructed from the client supplied values as well.</p>

    <p>An example where this may be useful is on an intranet server
    where you have users connecting to the machine using short
    names such as <code>www</code>. You'll notice that if the users
    type a shortname, and a URL which is a directory, such as
    <code>http://www/splat</code>, <em>without the trailing
    slash</em> then Apache httpd will redirect them to
    <code>http://www.domain.com/splat/</code>. If you have
    authentication enabled, this will cause the user to have to
    authenticate twice (once for <code>www</code> and once again
    for <code>www.domain.com</code> -- see <a
    href="http://httpd.apache.org/docs/misc/FAQ.html#prompted-twice">the
    FAQ on this subject for more information</a>). But if
    <directive>UseCanonicalName</directive> is set <code>Off</code>, then
    Apache httpd will redirect to <code>http://www/splat/</code>.</p>

    <p>There is a third option, <code>UseCanonicalName DNS</code>,
    which is intended for use with mass IP-based virtual hosting to
    support ancient clients that do not provide a
    <code>Host:</code> header. With this option Apache httpd does a
    reverse DNS lookup on the server IP address that the client
    connected to in order to work out self-referential URLs.</p>

    <note type="warning"><title>Warning</title>
    <p>If CGIs make assumptions about the values of <code>SERVER_NAME</code>
    they may be broken by this option. The client is essentially free
    to give whatever value they want as a hostname. But if the CGI is
    only using <code>SERVER_NAME</code> to construct self-referential URLs
    then it should be just fine.</p>
    </note>
</usage>
<seealso><directive module="core">UseCanonicalPhysicalPort</directive></seealso>
<seealso><directive module="core">ServerName</directive></seealso>
<seealso><directive module="mpm_common">Listen</directive></seealso>
</directivesynopsis>

<directivesynopsis>
<name>UseCanonicalPhysicalPort</name>
<description>Configures how the server determines its own name and
port</description>
<syntax>UseCanonicalPhysicalPort On|Off</syntax>
<default>UseCanonicalPhysicalPort Off</default>
<contextlist><context>server config</context><context>virtual host</context>
<context>directory</context></contextlist>

<usage>
    <p>In many situations Apache httpd must construct a <em>self-referential</em>
    URL -- that is, a URL that refers back to the same server. With
    <code>UseCanonicalPhysicalPort On</code> Apache httpd will, when
    constructing the canonical port for the server to honor
    the <directive module="core">UseCanonicalName</directive> directive,
    provide the actual physical port number being used by this request
    as a potential port. With <code>UseCanonicalPhysicalPort Off</code>
    Apache httpd will not ever use the actual physical port number, instead
    relying on all configured information to construct a valid port number.</p>

    <note><title>Note</title>
    <p>The ordering of when the physical port is used is as follows:<br /><br />
     <code>UseCanonicalName On</code></p>
     <ul>
      <li>Port provided in <code>Servername</code></li>
      <li>Physical port</li>
      <li>Default port</li>
     </ul>
     <code>UseCanonicalName Off | DNS</code>
     <ul>
      <li>Parsed port from <code>Host:</code> header</li>
      <li>Physical port</li>
      <li>Port provided in <code>Servername</code></li>
      <li>Default port</li>
     </ul>
    
    <p>With <code>UseCanonicalPhysicalPort Off</code>, the
    physical ports are removed from the ordering.</p>
    </note>

</usage>
<seealso><directive module="core">UseCanonicalName</directive></seealso>
<seealso><directive module="core">ServerName</directive></seealso>
<seealso><directive module="mpm_common">Listen</directive></seealso>
</directivesynopsis>

<directivesynopsis type="section">
<name>VirtualHost</name>
<description>Contains directives that apply only to a specific
hostname or IP address</description>
<syntax>&lt;VirtualHost
    <var>addr</var>[:<var>port</var>] [<var>addr</var>[:<var>port</var>]]
    ...&gt; ... &lt;/VirtualHost&gt;</syntax>
<contextlist><context>server config</context></contextlist>

<usage>
    <p><directive type="section">VirtualHost</directive> and
    <code>&lt;/VirtualHost&gt;</code> are used to enclose a group of
    directives that will apply only to a particular virtual host. Any
    directive that is allowed in a virtual host context may be
    used. When the server receives a request for a document on a
    particular virtual host, it uses the configuration directives
    enclosed in the <directive type="section">VirtualHost</directive>
    section. <var>Addr</var> can be:</p>

    <ul>
      <li>The IP address of the virtual host;</li>

      <li>A fully qualified domain name for the IP address of the
      virtual host (not recommended);</li>

      <li>The character <code>*</code>, which is used only in combination with
      <code>NameVirtualHost *</code> to match all IP addresses; or</li>

      <li>The string <code>_default_</code>, which is used only
      with IP virtual hosting to catch unmatched IP addresses.</li>
    </ul>

    <example><title>Example</title>
      &lt;VirtualHost 10.1.2.3&gt;<br />
      <indent>
        ServerAdmin webmaster@host.example.com<br />
        DocumentRoot /www/docs/host.example.com<br />
        ServerName host.example.com<br />
        ErrorLog logs/host.example.com-error_log<br />
        TransferLog logs/host.example.com-access_log<br />
      </indent>
      &lt;/VirtualHost&gt;
    </example>


    <p>IPv6 addresses must be specified in square brackets because
    the optional port number could not be determined otherwise.  An
    IPv6 example is shown below:</p>

    <example>
      &lt;VirtualHost [2001:db8::a00:20ff:fea7:ccea]&gt;<br />
      <indent>
        ServerAdmin webmaster@host.example.com<br />
        DocumentRoot /www/docs/host.example.com<br />
        ServerName host.example.com<br />
        ErrorLog logs/host.example.com-error_log<br />
        TransferLog logs/host.example.com-access_log<br />
      </indent>
      &lt;/VirtualHost&gt;
    </example>

    <p>Each Virtual Host must correspond to a different IP address,
    different port number or a different host name for the server,
    in the former case the server machine must be configured to
    accept IP packets for multiple addresses. (If the machine does
    not have multiple network interfaces, then this can be
    accomplished with the <code>ifconfig alias</code> command -- if
    your OS supports it).</p>

    <note><title>Note</title>
    <p>The use of <directive type="section">VirtualHost</directive> does
    <strong>not</strong> affect what addresses Apache httpd listens on. You
    may need to ensure that Apache httpd is listening on the correct addresses
    using <directive module="mpm_common">Listen</directive>.</p>
    </note>

    <p>When using IP-based virtual hosting, the special name
    <code>_default_</code> can be specified in
    which case this virtual host will match any IP address that is
    not explicitly listed in another virtual host. In the absence
    of any <code>_default_</code> virtual host the "main" server config,
    consisting of all those definitions outside any VirtualHost
    section, is used when no IP-match occurs.</p>

    <p>You can specify a <code>:port</code> to change the port that is
    matched. If unspecified then it defaults to the same port as the
    most recent <directive module="mpm_common">Listen</directive>
    statement of the main server. You may also specify <code>:*</code>
    to match all ports on that address. (This is recommended when used
    with <code>_default_</code>.)</p>

    <p>A <directive module="core">ServerName</directive> should be
    specified inside each <directive
    type="section">VirtualHost</directive> block. If it is absent, the
    <directive module="core">ServerName</directive> from the "main"
    server configuration will be inherited.</p>

    <p>If no matching virtual host is found, then the first listed
    virtual host that matches the IP address will be used.  As a
    consequence, the first listed virtual host is the default virtual
    host.</p>

    <note type="warning"><title>Security</title>
    <p>See the <a href="../misc/security_tips.html">security tips</a>
    document for details on why your security could be compromised if the
    directory where log files are stored is writable by anyone other
    than the user that starts the server.</p>
    </note>
</usage>
<seealso><a href="../vhosts/">Apache HTTP Server Virtual Host documentation</a></seealso>
<seealso><a href="../dns-caveats.html">Issues Regarding DNS and
    Apache HTTP Server</a></seealso>
<seealso><a href="../bind.html">Setting
    which addresses and ports Apache HTTP Server uses</a></seealso>
<seealso><a href="../sections.html">How &lt;Directory&gt;, &lt;Location&gt;
    and &lt;Files&gt; sections work</a> for an explanation of how these
    different sections are combined when a request is received</seealso>
</directivesynopsis>

</modulesynopsis>
