<?xml version='1.0' encoding='UTF-8' ?> <!DOCTYPE manualpage SYSTEM
"../style/manualpage.dtd"> <?xml-stylesheet type="text/xsl"
href="../style/manual.es.xsl"?> <!-- English Revision: 182473 5  --> <!--
Spanish translation : Luis Joaquin Gil de Bernabé Pfeiffer l --> <!--  Licensed
to the Apache Software Foundation (ASF) under one or more  contributor license
agreements.  See the NOTICE file distributed with  this work for additional
information regarding copyright ownership.  The ASF licenses this file to You
under the Apache License, Version 2.0  (the "License"); you may not use this
file except in compliance with  the License.  You may obtain a copy of the
License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,  WITHOUT
WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
License for the specific language governing permissions and  limitations under
the License. -->

<manualpage metafile="encrypt.xml.meta"> <parentdocument href="./">How-To /
Tutoriales</parentdocument>

  <title>Como Cifrar su Tráfico</title>

  <summary> 
    <p>En esta guía se explica cómo hacer que su servidor HTTPD Apache
  use un cifrado para transferir datos entre el servidor y sus visitantes. En vez
  de usar enlaces <code>http:</code>, usará del tipo<code>https:</code>, si todo
  está configurado correctamente, toda persona que visite su web, tendrá más
  privacidad y protección.</p>
  <p> Este manual está pensado para aquellos que no están muy familiarizados con
  SSL/TLS y cifrados, junto con toda la jerga técnica incomprensible (Estamos 
  bromeando, este tema es bastante importante, con
  serios expertos en el tema, y problemas reales que resolver - pero sí, suena a 
  jerga técnica incomprensible para todos aquellos que no hayan tratado con esto). 
  Personas que han escuchado que su servidor http: no es del todo seguro a dia de 
  hoy. Que los espías y los malos están escuchando. Que incluso las empresas 
  legítimas están insertando datos en sus páginas web y vendiendo perfiles de 
  visitantes. 
</p> 
<p>En esta guía nos centraremos en ayudarle para migrar su servidor httpd, para 
   que deje de servir enlaces vía <code>http:</code> y los sirva vía 
   <code>https:</code> ones, without you becoming a SSL expert first. You might
   get fascinated by all this crypto things and study it more and become a real
   expert. But you also might not, run a reasonably secure web server nevertheless
   and do other things good for mankind with your time. </p> <p> You
  will get a rough idea what roles these mysterious things called "certificate"
  and  "private key" play and how they are used to let your visitors be sure
  they are talking to your server. You will <em>not</em> be told <em>how</em>
  this works, just how it is used: it's basically about passports. </p>
  </summary> 
  <seealso><a href="../ssl/ssl_howto.html">SSL How-To</a></seealso>
  <seealso><a href="../mod/mod_ssl.html">mod_ssl</a></seealso> 
  <seealso><a href="../mod/mod_md.html">mod_md</a></seealso>

  <section id="protocol"> 
    <title>Pequeña introducción a Certificados e.j: Pasaporte de Internet</title> 

  <p> The TLS protocol (formerly known as SSL) is a
  way a client and a server  can talk to each other without anyone else
  listening, or better understanding a thing. It is what your browser uses when
  you open a https: link.  </p> <p> In addition to having a private conversation
  with a server, your browser also needs to know that it really talks to the
  server - and not someone else acting like it. That, next to the encryption, is
  the other part of the TLS protocol. </p> <p> In order to do that, your server
  does not only need the software for TLS, e.g. the <a
  href="../mod/mod_http2.html">mod_ssl</a> module, but some sort of identity
  proof on the Internet. This is commonly referred to as a <em>certificate</em>.
  Basically, everyone has the same mod_ssl and can encrypt, but only your have
  <em>your</em> certificate and with that, you are you. </p> <p> A certificate
  is the digital equivalent of a passport. It contains two things: a stamp of
  approval from the people issuing the passport and a reference to your digital
  fingerprints, e.g. what is called a <em>private key</em> in encryption terms.
  </p> <p> When you configure your Apache httpd for https: links, you need to
  give it the certificate and the private key. If you never give the key to
  anyone else, only you will be able to prove to visitors that the certificate
  belongs to you. That way, a browser talking to your server a second time will
  be sure that it is indeed the very same server it talked to before. </p> <p>
  But how does it know that it is the real server, the first time it starts
  talking to someone? Here, the digital rubber stamping comes into play. The
  rubber stamp is done by someone else, using her own private key. That person
  has also a certificate, e.g. her own passport. The browser can make sure that
  this passport is based on the same key that was used to rubber stamp your
  server passport. Now, instead of making sure that your passport is correct, it
  must make sure that the passport of the person that  says <em>your</em>
  passport is correct, is correct.  </p> <p> And that passport is also rubber
  stamped digitally, by someone else with a key and a  certificate. So the
  browser only needs to make sure that <em>that</em> one is correct that says it
  is correct to trust the one that says your server is correct. This trusting
  game can go to a few or many levels (usually less than 5). </p> <p> In the
  end, the browser will encounter a passport that is stamped by its own key.
  It's a Gloria Gaynor certificate that says "I am what I am!". The browser then
  either trust this Gloria or not. If not, your server is also not trusted.
  Otherwise, it is. Simple.  </p> <p> The trust check for the Gloria Gaynors of
  the Internet is easy: your browser (or your operating system) comes with list
  of Gloria passports to trust, pre-installed. If it  sees a Gloria certificate,
  it is either in this list or not to be trusted. </p> <p> This whole thing
  works as long as everyone keeps his private keys to himself. Anyone copying
  such a key can impersonate the key owner. And if the owner can rubber stamp
  passports, the impersonator can also do that. And all the passports stamped by
  an impersonator,  all those certificates will look 100% valid,
  indistinguishable from the "real" ones. </p> <p> So, this trust model works,
  but it has its limits. That is why browser makers are so keen on having the
  correct Gloria Gaynor lists and threaten to expel anyone from it that is
  careless with her keys. </p> </section>

  <section id="buycert"> <title>Comprar un Certificado</title> <p> Bueno, pueds
  comprar uno. Hay muchas compañias vendiando pasaportes de Internet como
  servicio. En  <a href="https://ccadb-
  public.secure.force.com/mozilla/IncludedCACertificateReport">esta lista de
  Mozilla,</a> podrás encontrar todas las compañias en las que el  navegador
  Firefox confía. Escoge una, visita su pagina web y te diran los diferentes
  precios, y como hacer para comprobar tu identidad y quien dices ser quien
  eres, y así podrán generar tu pasaporte con confianza. </p> <p>

    They all have their own methods, also depending on what kind of passport you
apply for, and     it's probably some sort of click web interface in a browser.
They may send you an email that     you need to answer or do something else. In
the end, they will show you how to generate     your own, unique private key and
issue you a stamped passport matching it.     </p>     <p>     You then place
the key in one file, the certificate in another. Put these on your server, make
sure that only a trusted user can read the key file and add it to your httpd
configuration.      This is extensively covered in the <a
href="../ssl/ssl_howto.html">SSL How-To</a>.     </p>     <p>     </p>
</section>

  <section id="freecert"> <title>Get a Free Certificate</title> <p> Hay también
  compañias que ofrecen certificados gratuitos para servidores web.  La pionera
  en esto es <a href="https://letsencrypt.org">Let's Encrypt</a>  que es un
  servicio de la organización sin ánimo de lucro <a href="">(ISRG)  Internet
  Security Research Group </a>, para "reducir las barreras financieras,
  tecnológicas y de educación, para securizar las comunicaciones en Internet."
  </p> <p> No sólo ofrencen certificados gratuitos, también han desaarrollado
  una  interfáz que puede ser usada en su Apache Httpd para obtener uno. Aquí es
  donde <a href="../mod/mod_md.html">mod_md</a> entra en juego. </p> <p> (zoom
  out the camera on how to configure mod_md and virtual host...) </p> </section>
    
</manualpage>
