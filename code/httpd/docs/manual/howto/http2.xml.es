<?xml version='1.0' encoding='UTF-8' ?>
<!DOCTYPE manualpage SYSTEM "../style/manualpage.dtd">
<?xml-stylesheet type="text/xsl" href="../style/manual.es.xsl"?>
<!-- English Revision: 1834263:1878547 (outdated) --> 
<!-- Spanish translation : Daniel Ferradal -->
<!-- Reviewed & updated by Luis Gil de Bernabé Pfeiffer lgilbernabe[AT]apache.org -->
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
<manualpage metafile="http2.xml.meta">
<parentdocument href="./">How-To / Tutoriales</parentdocument>

  <title>Guía HTTP/2</title>

  <summary>
    <p>
        Esta es la guía para configurar HTTP/2 en Apache httpd. Ésta característica
        está <em>lista en produción</em> así que es de esperar que las interfaces
        y las directivas se mantengan consistentes en cada verión.
    </p>
  </summary>
  <seealso><module>mod_http2</module></seealso>

  <section id="protocol">
    <title>El protocolo HTTP/2</title>

    <p>HTTP/2 es la evolución del protocolo de la capa de aplicación con más
    éxito, HTTP. Se centra en hacer un uso más eficiente de los recursos de red. 
    No cambia la característica fundamental de HTTP, la semántica. Todavía hay 
    olicitudes, respuestas, cabeceras y todo los elementos típicos de HTTP/1. Así 
    que, si ya conoce HTTP/1, también conoce el 95% de HTTP/2.</p>
    
    <p>Se ha escrito mucho sobre HTTP/2 y de cómo funciona. La norma más
    estándar es, por supuesto, su 
    <a href="https://tools.ietf.org/html/rfc7540">RFC 7540</a> 
    (<a href="http://httpwg.org/specs/rfc7540.html"> también disponible en un
    formato más legible, YMMV</a>). Así que, ahí encontrará toda la especificación 
    del protocolo.</p>

    <p>Pero, como con todos los RFC, no es ideal como primera lectura. Es mejor
    entender primero <em>qué</em> se quiere hacer y después leer el RFC sobre 
    <em>cómo</em> hacerlo. Un documento mucho mejor con el que empezar es
    <a href="https://daniel.haxx.se/http2/">http2 explicado</a>
    por Daniel Stenberg, el autor de <a href="https://curl.haxx.se">curl</a>. 
    ¡También está disponible cada vez en un mayor número lenguajes!</p>
    
    <p>Si le parece demasiado largo, o no lo ha leido, hay algunos términos
    y elementos a tener en cuenta cuando lea este documento:</p>
    <ul>
        <li>HTTP/2 es un <strong>protocolo binario</strong>, al contrario que 
        HTTP 1.1 que es texto plano. La intención para HTTP 1.1 es que sea 
        legible (por ejemplo capturando el tráfico de red) mientras que para 
        HTTP/2 no. Más información en el FAQ oficial 
        <a href="https://http2.github.io/faq/#why-is-http2-binary">¿Por qué es 
            binario HTTP/2?</a></li>

        <li><strong>h2</strong> es HTTP/2 sobre TLS (negociación de protocolo a 
        través de ALPN).</li>

        <li><strong>h2c</strong> es HTTP/2 sobre TCP.</li>

        <li>Un <strong>frame</strong> es la unidad más pequeña de comunicación 
        dentro de una conexión HTTP/2, que consiste en una cabecera y una secuencia 
        de octetos de longitud variable estructurada de acuerdo con el tipo de 
        frame. Más información en la documentación oficial 
        <a href="http://httpwg.org/specs/rfc7540.html#FramingLayer">Sección de 
            Capa de Frame</a>.</li>

        <li>Un <strong>stream</strong> es un flujo bidireccional de frames dentro 
        de una conexión HTTP/2. El concepto correspondiente en HTTP 1.1 es un 
        intercambio de mensajes de solicitud/respuesta. Más información en la 
        documentación oficial 
        <a href="http://httpwg.org/specs/rfc7540.html#StreamsLayer">Sección Capa 
            de Stream</a>.</li>

        <li>
            HTTP/2 es capaz de llevar <strong>múltiples streams</strong> de datos
            sobre la misma conexión TCP, evitando la clásica solicitud lenta 
            "head-of-line blocking" de HTTP 1.1 y evitando generar múltiples conexiones
            TCP para cada solicitud/respuesta (KeepAlive parcheó el problema en 
            HTTP 1.1 pero no lo resolvió completamente).
      </li>
    </ul>
  </section>

  <section id="implementation">
    <title>HTTP/2 en Apache httpd</title>

    <p>
        El protocolo HTTP/2 se implementa con su propio módulo httpd, llamado 
        acertadamente <module>mod_http2</module>. Incluye el set completo de 
        características descritas por el RFC 7540 y soporta HTTP/2 sobre texto 
        plano (http:), así como conexiones seguras (https:). La variante de texto
        plano se llama '<code>h2c</code>', la segura '<code>h2</code>'. Para 
        <code>h2c</code> permite el modo <em>direct</em>
        y el <code>Upgrade:</code> a través de una solicitud inicial HTTP/1.
    </p>
    
    <p>
        Una característica de HTTP/2 que ofrece capacidades nuevas para 
        desarrolladores de web es <a href="#push">Server Push</a>. Vea esa sección
         para saber como su aplicación web puede hacer uso de ella.
     </p>
  </section>
  
  <section id="building">
    <title>Compilar httpd con soporte HTTP/2</title>

    <p>
        <module>mod_http2</module> usa la librería <a href="https://nghttp2.org">
        nghttp2</a>como su implementación base. Para compilar 
        <module>mod_http2</module> necesita al menos la versión 1.2.1 de 
        <code>libnghttp2</code> instalada en su sistema.
    </p>

    <p>
        Cuando usted ejecuta <code>./configure</code> en el código fuente de 
        Apache HTTPD, necesita indicarle '<code>--enable-http2</code>' como una 
        opción adicional para activar la compilación de este módulo. Si su 
        <code>libnghttp2</code> está ubicado en una ruta no habitual (cualquiera que 
        sea en su sistema operativo), puede indicar su ubicación con 
        '<code>--with-nghttp2=&lt;path&gt;</code>' para <code>./configure</code>.
    </p>

    <p>Aunque puede que eso sirva para la mayoría, habrá quien prefiera un <code>nghttp2</code> compilado estáticamente para este módulo. Para ellos existe la opción <code>--enable-nghttp2-staticlib-deps</code>. Funciona de manera muy similar a como uno debe enlazar openssl estáticamente para <module>mod_ssl</module>.</p>

    <p>Hablando de SSL, necesita estar al tanto de que la mayoría de los navegadores hablan HTTP/2 solo con URLs <code>https:</code>. Así que necesita un servidor con soporte SSL. Pero no solo eso, necesitará una librería SSL que de soporte a la extensión <code>ALPN</code>. Si usa OpenSSL, necesita al menos la versión 1.0.2.</p>
  </section>

  <section id="basic-config">
    <title>Configuración básica</title>

    <p>Cuando tiene un <code>httpd</code> compilado con <module>mod_http2</module> necesita una configuración básica para activarlo. Lo primero, como con cualquier otro módulo de Apache, es que necesita cargarlo:</p>
    
    <highlight language="config">
LoadModule http2_module modules/mod_http2.so
    </highlight>
    
    <p>La segunda directiva que necesita añadir a la configuración de su servidor es:</p>

    <highlight language="config">
Protocols h2 http/1.1
    </highlight>
    
    <p>Esto permite h2, la variante segura, para ser el protocolo preferido de las conexiones en su servidor. Cuando quiera habilitar todas las variantes de HTTP/2, entonces simplemente configure:</p>

    <highlight language="config">
Protocols h2 h2c http/1.1
    </highlight>

    <p>Dependiendo de dónde pone esta directiva, afecta a todas las conexiones o solo a las de ciertos host virtuales. La puede anidar, como en:</p>

    <highlight language="config">
Protocols http/1.1
&lt;VirtualHost ...&gt;
    ServerName test.example.org
    Protocols h2 http/1.1
&lt;/VirtualHost&gt;
    </highlight>

    <p>Esto solo permite HTTP/1, excepto conexiones SSL hacia <code>test.example.org</code> que ofrecen HTTP/2.</p>

    <note><title>Escoger un SSLCipherSuite seguro</title>
     <p>Es necesario configurar <directive module="mod_ssl">SSLCipherSuite</directive> con una suite segura de cifrado TLS. La versión actual de mod_http2 no fuerza ningún cifrado pero la mayoría de los clientes si lo hacen. Encaminar un navegador hacia un servidor con <code>h2</code> activado con una suite inapropiada de cifrados forzará al navegador a rehusar e intentar conectar por HTTP 1.1. Esto es un error común cuando se configura httpd con HTTP/2 por primera vez, ¡así que por favor tenga en cuenta que debe evitar largas sesiones de depuración! Si quiere estar seguro de la suite de cifrados que escoja, por favor evite los listados en la <a href="http://httpwg.org/specs/rfc7540.html#BadCipherSuites">Lista Negra de TLS para HTTP/2</a>.</p>
    </note>

    <p>El orden de los protocolos mencionados también es relevante. Por defecto, el primero es el protocolo preferido. Cuando un cliente ofrece múltiples opciones, la que esté más a la izquierda será la escogida. En</p>
    <highlight language="config">
Protocols http/1.1 h2
    </highlight>
    
    <p>el protocolo preferido es HTTP/1 y siempre será seleccionado a menos que el cliente <em>sólo</em> soporte h2. Puesto que queremos hablar HTTP/2 con clientes que lo soporten, el orden correcto es:</p>
    
    <highlight language="config">
Protocols h2 h2c http/1.1
    </highlight>

    <p>Hay algo más respecto al orden: el cliente también tiene sus propias preferencias. Si quiere, puede configurar su servidor para seleccionar el protocolo preferido por el cliente:</p>

    <highlight language="config">
ProtocolsHonorOrder Off
    </highlight>

    <p>Hace que el orden en que <em>usted</em> escribió los Protocols sea irrelevante y sólo el orden de preferencia del cliente será decisorio.</p>

    <p>Una última cosa: cuando usted configura los protocolos no se comprueba si son correctos o están bien escritos. Puede mencionar protocolos que no existen, así que no hay necesidad de proteger <directive module="core">Protocols</directive> con ningún <directive type="section" module="core">IfModule</directive> de comprobación.</p>

    <p>Para más consejos avanzados de configuración, vea la <a href="../mod/mod_http2.html#dimensioning">
    sección de módulos sobre dimensionamiento</a> y <a href="../mod/mod_http2.html#misdirected">
    como gestionar multiples hosts con el mismo certificado</a>.</p>
  </section>

  <section id="mpm-config">
    <title>Configuración MPM</title>
    
    <p>HTTP/2 está soportado en todos los módulos de multi-proceso que se ofrecen con httpd. Aun así, si usa el mpm <module>prefork</module>, habrá  restricciones severas.</p>

    <p>En <module>prefork</module>, <module>mod_http2</module> solo procesará una solicitud cada vez por conexión. Pero los clientes, como los navegadores, enviarán muchas solicitudes al mismo tiempo. Si una de ellas tarda mucho en procesarse (o hace un sondeo que dura más de la cuenta), las otras solicitudes se quedarán atascadas.</p>

    <p><module>mod_http2</module> no evitará este límite por defecto. El motivo es que <module>prefork</module> hoy en día solo se escoge si ejecuta motores de proceso que no están preparados para multi-hilo, p.ej. fallará con más de una solicitud.</p>

    <p>Si su configuración lo soporta, hoy en día <module>event</module> es el mejor mpm que puede usar.</p>
    
    <p>Si realmente está obligado a usar <module>prefork</module> y quiere multiples solicitudes, puede configurar la directiva <directive module="mod_http2">H2MinWorkers</directive> para hacerlo posible. Sin embargo, si esto falla, es bajo su cuenta y riesgo.</p>
  </section>
  
  <section id="clients">
    <title>Clientes</title>
    
    <p>Casi todos los navegadores modernos dan soporte a HTTP/2, pero solo en conexiones SSL: Firefox (v43), Chrome (v45), Safari (since v9), iOS Safari (v9), Opera (v35), Chrome para Android (v49) e Internet Explorer (v11 en Windows10) (<a href="http://caniuse.com/#search=http2">Fuente</a>).</p>

    <p>Otros clientes, así cómo otros servidores, están listados en la 
    <a href="https://github.com/http2/http2-spec/wiki/Implementations">wiki de Implementaciones</a>, entre ellos, implementaciones para c, c++, common lisp, dart, erlang, haskell, java, nodejs, php, python, perl, ruby, rust, scala y swift.</p>

    <p>Muchos de las implementaciones de clientes que no son navegadores soportan HTTP/2 sobre texto plano, h2c. La más versátil es <a href="https://curl.haxx.se">curl</a>.</p>
  </section>

  <section id="tools">
    <title>Herramientas útiles para depurar HTTP/2</title>

    <p>La primera herramienta a mencionar es por supuesto <a href="https://curl.haxx.se">curl</a>. Por favor asegúrese de que su versión soporta HTTP/2 comprobando sus <code>Características</code>:</p>
    <highlight language="config">
    $ curl -V
    curl 7.45.0 (x86_64-apple-darwin15.0.0) libcurl/7.45.0 OpenSSL/1.0.2d zlib/1.2.8 nghttp2/1.3.4
    Protocols: dict file ftp ftps gopher http https imap imaps ldap ldaps pop3 [...] 
    Features: IPv6 Largefile NTLM NTLM_WB SSL libz TLS-SRP <strong>HTTP2</strong>
    </highlight>
    <note><title>Notas sobre Mac OS homebrew</title>
    brew install curl --with-openssl --with-nghttp2 
    </note>
    <p>Y para una inspección en gran profundidad <a href="https://wiki.wireshark.org/HTTP2">wireshark</a>.</p>
    <p>El paquete <a href="https://nghttp2.org">nghttp2</a> también incluye clientes, tales como:</p>
    <ul>
        <li><a href="https://nghttp2.org/documentation/nghttp.1.html">nghttp
        </a> - util para visualizar la frames de HTTP/2 y tener una mejor idea de como funciona el protocolo.</li>
        <li><a href="https://nghttp2.org/documentation/h2load-howto.html">h2load</a> - útil para hacer un stress-test de su servidor.</li>
    </ul>

    <p>Chrome ofrece logs detallados de HTTP/2 en sus conexiones a través de la <a href="chrome://net-internals/#http2">página especial de net-internals</a>. También hay una extensión interesante para <a href="https://chrome.google.com/webstore/detail/http2-and-spdy-indicator/mpbpobfflnpcgagjijhmgnchggcjblin?hl=en">Chrome</a> y <a href="https://addons.mozilla.org/en-us/firefox/addon/spdy-indicator/">Firefox</a> con la que visualizar cuando su navegador usa HTTP/2.</p>
  </section>

  <section id="push">
    <title>Server Push</title>
    
    <p>El protocolo HTTP/2 permite al servidor hacer PUSH de respuestas a un cliente que nunca las solicitó. El tono de la conversación es: &quot;Aquí tiene una solicitud que nunca envió y la respuesta llegará pronto...&quot;</p>

    <p>Pero hay restricciones: el cliente puede deshabilitar esta característica y el servidor entonces solo podrá hacer PUSH en una solicitud que hizo previamente del cliente.</p>

    <p>La intención es permitir al servidor enviar recursos que el cliente seguramente vaya a necesitar, p. ej. un recurso css o javascript que pertenece a una página html que el cliente solicitó, un grupo de imágenes a las que se hace referencia en un css, etc.</p>

    <p>La ventaja para el cliente es que ahorra tiempo para solicitudes que pueden tardar desde unos pocos milisegundos a medio segundo, dependiendo de la distancia entre el cliente y el servidor. La desventaja es que el cliente puede recibir cosas que ya tiene en su cache. Por supuesto que HTTP/2 soporta cancelación previa de tales solicitudes, pero aun así se malgastan recursos.</p>

    <p>Resumiendo: no hay una estrategia mejor sobre cómo usar esta característica de HTTP/2 y todo el mundo está experimentando con ella. Así que, ¿cómo experimenta usted con ella en Apache httpd?</p>

    <p><module>mod_http2</module> busca e inspecciona las cabeceras de respuesta 
    <code>Link</code> con cierto formato:</p>

    <highlight language="config">
Link &lt;/xxx.css&gt;;rel=preload, &lt;/xxx.js&gt;; rel=preload
    </highlight>

    <p>
        Si la conexión soporta PUSH, estos dos recursos se enviarán al cliente. 
        Como desarrollador web, puede configurar estas cabeceras o bien 
        directamente en la respuesta de su aplicación o configurar su servidor con:
    </p>

    <highlight language="config">
&lt;Location /xxx.html&gt;
    Header add Link "&lt;/xxx.css&gt;;rel=preload"
    Header add Link "&lt;/xxx.js&gt;;rel=preload"
&lt;/Location&gt;
    </highlight>

    <p>Si quiere usar enlaces con <code>preload</code> sin activar un PUSH, puede
    usar el parámetro <code>nopush</code>, como en:</p>

    <highlight language="config">
        Link &lt;/xxx.css&gt;;rel=preload;nopush
    </highlight>

    <p>o puede desactivar PUSH para su servidor por completo con la directiva </p>

    <highlight language="config">
H2Push Off
    </highlight>

    <p>Y hay más:</p>

    <p>
        El módulo mantiene un registro de lo que se ha enviado con PUSH para cada
        conexión (hashes de URLs, básicamente) y no hará PUSH del mismo recurso dos
        veces. Cuando la conexión se cierra, la información es descartada.
    </p>

    <p>
        Hay gente pensando cómo un cliente puede decirle al servidor lo que ya
        tiene, para evitar los PUSH de esos elementos, pero eso algo muy
        experimental ahora mismo.
    </p>

    <p>Otro borrador experimental que ha sido implementado en 
    <module>mod_http2</module> es el <a href="https://tools.ietf.org/html/draft-ruellan-http-accept-push-policy-00"> Campo de Cabecera
    Accept-Push-Policy</a> en la que un cliente puede, para cada solicitud, definir 
    qué tipo de PUSH acepta.</p>

    <p>
        Puede que PUSH no siempre lance la peticion/respuesta/funcionamiento que
        uno espera. Hay varios estudios sobre este tema en internet, que explican
        el beneficio y las debilidades de como diferentes funcionalidades del
        cliente y de la red influyen en el resultado.
        Por Ejemplo, que un servidor haga "PUSH" de recursos, no significa que el 
        navegador vaya a usar dichos datos.
    </p>
    <p>
        Lo más importante que influye en la respuesta que se envía, es la solicitud
        que se simuló. La url de solicitud de un PUSH es dada por la aplicación,
        pero ¿de donde vienen las cabeceras de la petición? por ejemplo si el PUSH
        pide una cabecera <code>accept-language</code> y si es así, ¿con qué valor?
    </p>
    <p>Httpd mirará la petición original (la que originó el PUSH) y copiará las
        siguientes cabeceras a las peticiones PUSH:
        <code>user-agent</code>, <code>accept</code>, <code>accept-encoding</code>,
        <code>accept-language</code>, <code>cache-control</code>.
    </p>
    <p>
        Todas las otras cabeceras son ignorados. Las cookies tampoco serán copiadas.
        Impulsar los recursos que requieren una cookie para estar presente no
        funcionará. Esto puede ser una cuestión de debate. Pero a menos que esto se
        discuta más claramente con el navegador, evitemos el exceso de precaución y
        no expongamos las cookies donde podrían o no ser visibles.
    </p>

</section>
<section id="earlyhints">
    <title>"Early Hints"</title>

    <p>Una alternativa de "Pushear" recursos es mandar una cabecera 
        <code>Link</code> al cliente antes que la respuesta esté lista. Esto usa
        una caracteristica de HTTP que se llama  "Early Hints" y está descrita en
        la <a href="https://tools.ietf.org/html/rfc8297">RFC 8297</a>.</p>
    <p>Para poder usar esto, necesita habilitarlo explicitamente en el servidor 
    via</p>
    
    <highlight language="config">
H2EarlyHints on
    </highlight>
    
    <p>(No está habilitado por defecto ya q ue algunos navegadores más antiguos 
        se caen con dichas  respuestas.)
    </p>

    <p>si esta funcionalidad esta activada, puede usar la directiva 
        <directive module="mod_http2">H2PushResource</directive> para que lance 
        "Early hints" y recursos mediante push:
    </p>
    <highlight language="config">
&lt;Location /xxx.html&gt;
    H2PushResource /xxx.css
    H2PushResource /xxx.js
&lt;/Location&gt;
    </highlight>
    <p>
        Esto lanzará una respuesta <code>"103 Early Hints"</code> a un cliente 
        tan pronto como el servidor <em>comience</em> a procesar la solicitud. 
        Esto puede ser mucho antes que en el momento en que se determinaron los 
        primeros encabezados de respuesta, dependiendo de su aplicación web.
    </p>

    <p>
        Si la directiva <directive module="mod_http2">H2Push</directive> está 
        habilitada, esto comenzará el PUSH justo después de la respuesta 103.
        Sin embargo, si la directiva <directive module="mod_http2">H2Push</directive> está dehabilitada, la respuesta 103 se le enviará al cliente.
    </p>
  </section>


  
</manualpage>
