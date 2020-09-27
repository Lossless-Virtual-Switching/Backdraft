<?xml version="1.0"?>

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

<!DOCTYPE xsl:stylesheet [
    <!ENTITY lf SYSTEM "util/lf.xml">
    <!ENTITY nbsp SYSTEM "util/nbsp.xml">
    <!ENTITY para SYSTEM "util/para.xml">
    <!ENTITY % HTTPD-VERSION SYSTEM "../version.ent">
    %HTTPD-VERSION;
]>
<xsl:stylesheet version="1.0"
              xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                  xmlns="http://www.w3.org/1999/xhtml">

<!--                                                                      -->
<!-- Please, don't hard-code output strings! Use the language             -->
<!-- files and the translation "stuff"...                                 -->
<!--                                                                      -->

<!-- Injected variables:                                                  -->
<!--   $is-retired      - (boolean) Is this httpd version retired?        -->
<!--   $is-chm          - (boolean) target is for CHM generation or not   -->
<!--   $is-zip          - (boolean) target is for ZIP generation or not   -->
<!--   $message         - (node-set) localized common text snippets       -->
<!--   $doclang         - (string) document language                      -->
<!--   $output-encoding - (string) MIME charset name of the output        -->
<!--                      encoding                                        -->

<!-- Constants used for case translation -->
<xsl:variable name="lowercase" select="'abcdefghijklmnopqrstuvwxyz'" />
<xsl:variable name="uppercase" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'" />

<!-- space separated list of blockelements defined in common.dtd -->
<!--    used for inline content catching in <example>s           -->
<xsl:variable name="blockelements">
    p  example  note  table  ul  ol  dl  pre  highlight img  blockquote
</xsl:variable>

<!-- relative path to /manual/ -->
<xsl:variable name="metafile" select="document(/*/@metafile)/metafile" />
<xsl:variable name="path"     select="$metafile/relpath" />

<!-- load outsourced page types -->
<xsl:include href="moduleindex.xsl" />
<xsl:include href="directiveindex.xsl" />
<xsl:include href="manualpage.xsl" />
<xsl:include href="synopsis.xsl" />
<xsl:include href="sitemap.xsl" />
<xsl:include href="indexpage.xsl" />
<xsl:include href="quickreference.xsl" />
<xsl:include href="faq.xsl" />
<xsl:include href="overrideindex.xsl" />

<!-- load utility snippets -->
<xsl:include href="util/modtrans.xsl" />
<xsl:include href="util/pretrim.xsl" />

<!-- make sure, we set relative anchors only, if we're actually -->
<!-- transforming a modulefile (see <directive> template)       -->
<xsl:variable name="in-modulesynopsis" select="boolean(/modulesynopsis)" />
<xsl:variable name="upgrade" select="boolean(/*/@upgrade)" />

<!-- when referencing to a directory, we may need to complete the path -->
<!-- with the index file (for offline applications like *.chm files)   -->
<xsl:variable name="index-file">
    <xsl:if test="$is-chm or $is-zip">index.html</xsl:if>
</xsl:variable>

<!-- it may be desired to open external targets in a new window -->
<xsl:variable name="ext-target" select="boolean($is-chm)" />

<xsl:template match="/">
    <xsl:if test="not($metafile/@reference)">
        <xsl:message terminate="yes">
            Invalid metafile. Probably your build system is not up-to-date.
            Get a current version and try again.
        </xsl:message>
    </xsl:if>
    <xsl:apply-templates />
</xsl:template>


<!-- #################################################################### -->
<!-- Utility templates for constructing pages                             -->
<!-- #################################################################### -->

<!-- ==================================================================== -->
<!-- HTML head                                                            -->
<!-- ==================================================================== -->
<xsl:template name="head">
<head>
    &lf;
    <meta http-equiv="Content-Type"
          content="text/html; charset={$output-encoding}" />&lf;
    <xsl:if test="not($is-chm or $is-zip)">
        <xsl:comment>
            &lf;
            <xsl:text>        </xsl:text>
            <xsl:text>XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX</xsl:text>
            <xsl:text>XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX</xsl:text>
            &lf;
            <xsl:text>              </xsl:text>
            <xsl:text>This file is generated from xml source: </xsl:text>
            <xsl:text>DO NOT EDIT</xsl:text>
            &lf;
            <xsl:text>        </xsl:text>
            <xsl:text>XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX</xsl:text>
            <xsl:text>XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX</xsl:text>
            &lf;
            <xsl:text>      </xsl:text>
        </xsl:comment>&lf;
    </xsl:if>

    <title>
        <xsl:choose>
        <xsl:when test="name">
            <xsl:value-of select="name"/>
        </xsl:when>

        <xsl:otherwise>
            <xsl:value-of select="title"/>
        </xsl:otherwise>
        </xsl:choose>

        <xsl:text> </xsl:text>
        <xsl:value-of select="normalize-space($message[@id='apachetitle'])"/>
        <xsl:text> </xsl:text>
        <xsl:value-of select="normalize-space($message[@id='version'])"/> 
    </title>&lf;

    <!-- chm files get a slightly different stylesheet -->
    <xsl:choose>
    <xsl:when test="$is-chm">
        <link type="text/css" rel="stylesheet" media="all"
              href="{$path}/style/css/manual-chm.css" />
    </xsl:when>
    <!-- zip packages do also -->
    <xsl:when test="$is-zip">
        <link title="Main stylesheet"  type="text/css" media="all"
                rel="stylesheet"
               href="{$path}/style/css/manual-zip.css" />&lf;
        <link title="No Sidebar - Default font size" type="text/css" media="all"
               rel="alternate stylesheet"
               href="{$path}/style/css/manual-zip-100pc.css"/>
    </xsl:when>
    <xsl:otherwise>
        <link title="Main stylesheet"  type="text/css" media="all"
                rel="stylesheet"
               href="{$path}/style/css/manual.css" />&lf;
        <link title="No Sidebar - Default font size" type="text/css" media="all"
               rel="alternate stylesheet"
               href="{$path}/style/css/manual-loose-100pc.css"/>
        
    </xsl:otherwise>
    </xsl:choose>&lf;

    <link type="text/css" media="print"
           rel="stylesheet"
           href="{$path}/style/css/manual-print.css"/>
    <link href="{$path}/style/css/prettify.css" type="text/css" rel="stylesheet" />&lf;
    <script type="text/javascript" src="{$path}/style/scripts/prettify.min.js">&lf;</script>&lf;
    <!-- chm files do not need a favicon -->
    <xsl:if test="not($is-chm or $is-zip)">&lf;
        <link rel="shortcut icon" href="{$path}/images/favicon.ico" />
        <xsl:if test="$is-retired">
            <xsl:choose>
            <xsl:when test="$upgrade">
                <xsl:if test="not(/*/@upgrade = '')">
                    <link rel="canonical" href="http://httpd.apache.org/docs/current{concat($metafile/path, /*/@upgrade, '.html')}"/>
                </xsl:if>
            </xsl:when>
            <xsl:otherwise>
                <link rel="canonical" href="http://httpd.apache.org/docs/current{concat($metafile/path, $metafile/basename, '.html')}"/>
            </xsl:otherwise>
            </xsl:choose>
        </xsl:if>
    </xsl:if>
</head>
</xsl:template>
<!-- /head -->


<!-- ==================================================================== -->
<!-- page top                                                             -->
<!-- ==================================================================== -->
<xsl:template name="top">
<div id="page-header">&lf;
    <xsl:call-template name="super-menu" />&lf;

    <p class="apache">
        <xsl:value-of select="normalize-space($message
                                              [@id='apachehttpserver'])"/>
    </p>&lf;

    <img src="{$path}/images/feather.png" alt="" />
</div>&lf; <!-- /page-header -->

<div class="up">
    <a href="./{$index-file}">
        <xsl:if test="parentdocument">
            <xsl:attribute name="href">
                <xsl:value-of select="parentdocument/@href"/>
            </xsl:attribute>

            <xsl:call-template name="helper.uri.fix">
                <xsl:with-param name="uri" select="parentdocument/@href" />
            </xsl:call-template>
      </xsl:if>

      <img src="{$path}/images/left.gif" alt="&lt;-" title="&lt;-" />
    </a>
</div>&lf;

<div id="path">&lf;
    <a href="http://www.apache.org/">
        <xsl:if test="$ext-target">
            <xsl:attribute name="target">_blank</xsl:attribute>
        </xsl:if>
        <xsl:value-of select="$message[@id='apache']" />
    </a>

    <xsl:text> &gt; </xsl:text>

    <a href="http://httpd.apache.org/">
        <xsl:if test="$ext-target">
            <xsl:attribute name="target">_blank</xsl:attribute>
        </xsl:if>
        <xsl:value-of select="$message[@id='http-server']" />
    </a>

    <xsl:text> &gt; </xsl:text>

    <a href="http://httpd.apache.org/docs/">
        <xsl:if test="$ext-target">
            <xsl:attribute name="target">_blank</xsl:attribute>
        </xsl:if>
        <xsl:value-of select="$message[@id='documentation']" />
    </a>

    <xsl:if test="not(../indexpage)">
    <xsl:text> &gt; </xsl:text>

    <a href="{$path}/{$index-file}">
        <xsl:value-of select="$message[@id='version']"/>
    </a>
    </xsl:if>

    <xsl:if test="../modulesynopsis or ../directiveindex or ../quickreference">
    <xsl:text> &gt; </xsl:text>

    <a href="./{$index-file}">
        <xsl:value-of select="$message[@id='modules']"/>
    </a>
    </xsl:if>

    <xsl:if test="parentdocument/text()">
    <xsl:text> &gt; </xsl:text>

    <a href="{parentdocument/@href}">
        <xsl:call-template name="helper.uri.fix">
            <xsl:with-param name="uri" select="parentdocument/@href"/>
        </xsl:call-template>
        <xsl:value-of select="parentdocument"/>
    </a>
    </xsl:if>
</div> <!-- /path -->
</xsl:template>
<!-- /top -->


<!-- ==================================================================== -->
<!-- retired                                                              -->
<!-- ==================================================================== -->
<xsl:template name="retired">
<xsl:if test="$is-retired">
    <xsl:variable name="base">
        <xsl:choose>
        <xsl:when test="$upgrade">
            <xsl:if test="not(/*/@upgrade = '')">
                <xsl:value-of select="/*/@upgrade" />
            </xsl:if>
        </xsl:when>
        <xsl:otherwise>
            <xsl:value-of
                select="$metafile/basename" />
        </xsl:otherwise>
        </xsl:choose>
    </xsl:variable>
    <xsl:variable name="future">
        <xsl:choose>
        <xsl:when test="$base = 'index'">
            <xsl:value-of select="$metafile/path" />
        </xsl:when>
        <xsl:when test="$base = ''">
            <!-- nothing -->
        </xsl:when>
        <xsl:otherwise>
            <xsl:value-of select="concat($metafile/path, $base, '.html')" />
        </xsl:otherwise>
        </xsl:choose>
    </xsl:variable>

    <div class="retired">
        <h4><xsl:value-of select="$message[@id='retired.headline']" /></h4>
        <xsl:apply-templates select="$message[@id='retired.description']" />
        <ul>
            <li><a href="http://httpd.apache.org/docs/current/">
                <xsl:value-of select="$message[@id='retired.current']" /></a>
            </li>
        </ul>
        <xsl:if test="not($future = '')">
            <p><xsl:apply-templates select="$message[@id='retired.document']" mode="retired" /></p>
        </xsl:if>
    </div>
</xsl:if>
</xsl:template>
<!-- /retired -->

<xsl:template match="message">
    <xsl:apply-templates />
</xsl:template>

<xsl:template match="link" mode="retired">
<xsl:variable name="base">
    <xsl:choose>
    <xsl:when test="$upgrade">
        <xsl:if test="not(/*/@upgrade = '')">
            <xsl:value-of select="/*/@upgrade" />
        </xsl:if>
    </xsl:when>
    <xsl:otherwise>
        <xsl:value-of
            select="$metafile/basename" />
    </xsl:otherwise>
    </xsl:choose>
</xsl:variable>
<xsl:variable name="future">
    <xsl:choose>
    <xsl:when test="$base = 'index'">
        <xsl:value-of select="$metafile/path" />
    </xsl:when>
    <xsl:when test="$base = ''">
        <!-- nothing -->
    </xsl:when>
    <xsl:otherwise>
        <xsl:value-of select="concat($metafile/path, $base, '.html')" />
    </xsl:otherwise>
    </xsl:choose>
</xsl:variable>

<a href="http://httpd.apache.org/docs/current{$future}">
    <xsl:apply-templates />
</a>
</xsl:template>


<!-- ==================================================================== -->
<!-- out of date                                                          -->
<!-- ==================================================================== -->
<xsl:template name="outofdate">
<xsl:if test="$metafile/variants/variant[.=$doclang]/@outdated = 'yes'">
    &lf;
    <div class="outofdate">
        <xsl:value-of select="$message[@id='outofdate']"/>
    </div>
</xsl:if>
</xsl:template>


<!-- ==================================================================== -->
<!-- page bottom                                                          -->
<!-- ==================================================================== -->
<xsl:template name="bottom">
<xsl:call-template name="langavail">
    <xsl:with-param name="position" select="'bottom'" />
</xsl:call-template>
<xsl:choose>
<xsl:when test="not($is-chm or $is-zip or $metafile/basename = 'index')">
<div class="top"><a href="#page-header"><img alt="top" src="{$path}/images/up.gif" /></a></div>
<div class="section">
<h2><a name="comments_section" id="comments_section"><xsl:value-of select="$message[@id='comments']" /></a></h2>
<div class="warning"><strong>Notice:</strong><br/>This is not a Q&amp;A section. Comments placed here should be pointed towards suggestions on improving the documentation or server, and may be removed by our moderators if they are either implemented or considered invalid/off-topic. Questions on how to manage the Apache HTTP Server should be directed at either our IRC channel, #httpd, on Freenode, or sent to our <a href="https://httpd.apache.org/lists.html">mailing lists</a>.</div>&lf;
<script type="text/javascript">
<xsl:text disable-output-escaping="yes"><![CDATA[<!--//--><![CDATA[//><!--
var comments_shortname = 'httpd';
var comments_identifier = 'http://httpd.apache.org/docs/]]></xsl:text>&httpd.comments;<xsl:value-of select="concat($metafile/path, $metafile/basename, '.html')" disable-output-escaping="yes" /><xsl:text disable-output-escaping="yes"><![CDATA[';
(function(w, d) {
    if (w.location.hostname.toLowerCase() == "httpd.apache.org") {
        d.write('<div id="comments_thread"><\/div>');
        var s = d.createElement('script');
        s.type = 'text/javascript';
        s.async = true;
        s.src = 'https://comments.apache.org/show_comments.lua?site=' + comments_shortname + '&page=' + comments_identifier;
        (d.getElementsByTagName('head')[0] || d.getElementsByTagName('body')[0]).appendChild(s);
    }
    else {
        d.write('<div id="comments_thread">Comments are disabled for this page at the moment.<\/div>');
    }
})(window, document);
//--><!]]]]>></xsl:text></script>
</div>
</xsl:when>
</xsl:choose>
<div id="footer">&lf;
    <p class="apache">
        <xsl:text>Copyright 2020 The Apache Software Foundation.</xsl:text><br />
        <xsl:if test="normalize-space($message[@id='before-license'])">
            <xsl:value-of select="$message[@id='before-license']"/>
            <xsl:text> </xsl:text>
        </xsl:if>

        <a href="http://www.apache.org/licenses/LICENSE-2.0">
            <xsl:if test="$ext-target">
                <xsl:attribute name="target">_blank</xsl:attribute>
            </xsl:if>
            <xsl:text>Apache License, Version 2.0</xsl:text>
        </a>
        <xsl:if test="normalize-space($message[@id='after-license'])">
            <xsl:text> </xsl:text>
            <xsl:value-of select="$message[@id='after-license']"/>
        </xsl:if>

        <xsl:text>.</xsl:text>
    </p>&lf;
    <xsl:call-template name="super-menu"/>

</div> <!-- /footer -->

<script type="text/javascript">
<xsl:text disable-output-escaping="yes"><![CDATA[<!--//--><![CDATA[//><!--
if (typeof(prettyPrint) !== 'undefined') {
    prettyPrint();
}
//--><!]]]]>></xsl:text></script>
</xsl:template>
<!-- /bottom -->


<!-- ==================================================================== -->
<!-- build an "available languages" section                               -->
<!-- ==================================================================== -->
<xsl:template name="langavail">
<xsl:param name="position" select="'top'" />

<xsl:if test="not($is-chm or $is-zip)">
<div class="{$position}lang">&lf;
    <p>
        <span>
            <xsl:value-of select="$message[@id='langavail']" />
            <xsl:text>: </xsl:text>
        </span>

        <xsl:for-each select="$metafile/variants/variant">
        <xsl:sort select="." />

            <a href="{$path}/{.}{$metafile/path}{$metafile/basename}.html">
                <xsl:if test="$metafile/basename = 'index'">
                    <xsl:attribute name="href">
                        <xsl:value-of
                            select="concat($path, '/', ., $metafile/path)" />
                    </xsl:attribute>
                </xsl:if>
                <xsl:if test="$doclang != .">
                    <xsl:attribute name="hreflang">
                        <xsl:value-of select="." />
                    </xsl:attribute>
                    <xsl:attribute name="rel">alternate</xsl:attribute>
                </xsl:if>
                <xsl:attribute name="title">
                    <xsl:value-of select="document(concat('../lang/', .,
                                                   '.xml'))
                                          /language/messages/message
                                          [@id='nativename']" />
                </xsl:attribute>

                &nbsp;
                <xsl:value-of select="." />
                &nbsp;
            </a>
            <xsl:if test="position() != last()">
                <xsl:text> |&#xA;</xsl:text>
            </xsl:if>
        </xsl:for-each>
    </p>&lf;
</div> <!-- /.{$position}lang -->
</xsl:if>

<xsl:if test="$position = 'top'">
    <xsl:call-template name="outofdate" />
</xsl:if>

</xsl:template>
<!-- /langavail -->


<!-- ==================================================================== -->
<!-- Process a documentation section                                      -->
<!-- ==================================================================== -->
<xsl:template match="section">
<xsl:call-template name="toplink" />&lf;
<div class="section">&lf;

    <!-- Section heading -->
    <h2>
        <xsl:choose>
        <xsl:when test="@id">
          <a id="{@id}" name="{@id}">
              <xsl:apply-templates select="title" mode="print" />
          </a>
          <xsl:text> </xsl:text>
          <a class="permalink" href="#{@id}" title="{$message[@id='permalink']}">&para;</a>
        </xsl:when>

        <xsl:otherwise>
            <xsl:apply-templates select="title" mode="print" />
        </xsl:otherwise>
        </xsl:choose>
    </h2>

    <!-- Section body -->
    <xsl:apply-templates />
</div> <!-- /.section -->
</xsl:template>
<!-- /section -->


<!-- ==================================================================== -->
<!-- handle subsections (lower level headings)                            -->
<!-- ==================================================================== -->
<xsl:template match="section/section" priority="3">
<!-- Section heading -->
<h3>
    <xsl:choose>
    <xsl:when test="@id">
        <a id="{@id}" name="{@id}">
            <xsl:apply-templates select="title" mode="print" />
        </a>
    </xsl:when>

    <xsl:otherwise>
        <xsl:apply-templates select="title" mode="print" />
    </xsl:otherwise>
    </xsl:choose>
</h3>

<!-- Section body -->
<xsl:apply-templates />
</xsl:template>
<!-- /section/section -->


<!-- ==================================================================== -->
<!-- handle subsubsections (h4)                                           -->
<!-- ==================================================================== -->
<xsl:template match="section/section/section" priority="4">
<!-- Section heading -->
<h4>
    <xsl:choose>
    <xsl:when test="@id">
        <a id="{@id}" name="{@id}">
            <xsl:apply-templates select="title" mode="print" />
        </a>
    </xsl:when>

    <xsl:otherwise>
        <xsl:apply-templates select="title" mode="print" />
    </xsl:otherwise>
    </xsl:choose>
</h4>

<!-- Section body -->
<xsl:apply-templates/>

</xsl:template>
<!-- /section/section/section -->


<!-- ==================================================================== -->
<!-- section nesting > h4 is not supported for now                        -->
<!-- ==================================================================== -->
<xsl:template match="section/section/section/section">
<xsl:message terminate="yes">
    <xsl:text>FATAL: exceeding maximum section nesting level.</xsl:text>
    &lf;&lf;
    <xsl:text>Perhaps you should consider to split your document into</xsl:text>
    &lf;
    <xsl:text>several ones...</xsl:text>
    &lf;
</xsl:message>
</xsl:template>
<!-- /section/section/section/section -->



<!-- ==================================================================== -->
<!-- Render trimmed pre/highlight-text                                    -->
<!-- ==================================================================== -->
<xsl:template name="pre">
<xsl:choose>
<!-- Simple case: only one text node -->
<xsl:when test="node()[position() = 1 and self::text()] and count(node()) = 1">
    <xsl:call-template name="pre-ltrim-one">
        <xsl:with-param name="string">
            <xsl:call-template name="pre-rtrim">
                <xsl:with-param name="string">
                    <xsl:call-template name="pre-ltrim">
                        <xsl:with-param name="string"
                            select="node()[position() = 1 and self::text()]" />
                    </xsl:call-template>
                </xsl:with-param>
            </xsl:call-template>
        </xsl:with-param>
    </xsl:call-template>
</xsl:when>

<!-- multiple nodes -->
<xsl:otherwise>
    <xsl:variable name="from">
        <xsl:choose>
        <xsl:when test="node()[position() = 1 and self::text()]">
            <xsl:value-of select="2" />
        </xsl:when>
        <xsl:otherwise>
            <xsl:value-of select="1" />
        </xsl:otherwise>
        </xsl:choose>
    </xsl:variable>
    <xsl:variable name="to">
        <xsl:choose>
        <xsl:when test="node()[position() = last() and self::text()]">
            <xsl:value-of select="count(node()) - 1" />
        </xsl:when>
        <xsl:otherwise>
            <xsl:value-of select="count(node())" />
        </xsl:otherwise>
        </xsl:choose>
    </xsl:variable>

    <xsl:if test="$from = 2">
        <xsl:choose>
        <xsl:when test="text()[contains(., '&#x0a;')]">
            <xsl:call-template name="pre-ltrim">
                <xsl:with-param name="string"
                    select="node()[position() = 1 and self::text()]" />
            </xsl:call-template>
        </xsl:when>
        <xsl:otherwise>
            <xsl:variable name="tmp" select="node()[position() = 1 and self::text()]" />
            <xsl:value-of select="substring($tmp, string-length(substring-before($tmp, substring(normalize-space($tmp), 1, 1))) + 1, string-length($tmp))" />
        </xsl:otherwise>
        </xsl:choose>
    </xsl:if>

    <xsl:apply-templates select="node()[position() &gt;= $from and position() &lt;= $to]" />

    <xsl:if test="$to &lt; count(node())">
        <xsl:call-template name="pre-rtrim">
            <xsl:with-param name="string"
                select="node()[position() = last() and self::text()]" />
        </xsl:call-template>
    </xsl:if>
</xsl:otherwise>
</xsl:choose>
</xsl:template>


<!-- ==================================================================== -->
<!-- Process source code highlighting                                     -->
<!-- ==================================================================== -->
<xsl:template match="highlight">
<pre class="prettyprint lang-{@language}">
    <xsl:call-template name="pre" />
</pre>&lf; <!-- /.highlight -->
</xsl:template>
<!-- /highlight -->


<!-- ==================================================================== -->
<!-- (sub)section titles                                                  -->
<!-- ==================================================================== -->
<xsl:template match="section/title" mode="print">
<xsl:apply-templates/>
</xsl:template>
<xsl:template match="section/title" />
<!-- /section/title -->


<!-- ==================================================================== -->
<!-- generate section index                                               -->
<!-- ==================================================================== -->
<xsl:template match="section" mode="index">
<li>
    <img src="{$path}/images/down.gif" alt="" />
    <xsl:text> </xsl:text>
    <xsl:choose>
    <xsl:when test="@id">
        <a href="#{@id}">
            <xsl:apply-templates select="title" mode="print" />
        </a>
    </xsl:when>
    <xsl:otherwise>
        <xsl:apply-templates select="title" mode="print" />
    </xsl:otherwise>
    </xsl:choose>
</li>&lf;
</xsl:template>
<!-- /section index -->


<!-- ==================================================================== -->
<!-- docs super menu                                                      -->
<!-- ==================================================================== -->
<xsl:template name="super-menu">
<p class="menu">
    <a href="{$path}/mod/{$index-file}">
        <xsl:value-of select="$message[@id='modules']" />
    </a>

    <xsl:text> | </xsl:text>

    <a href="{$path}/mod/quickreference.html">
        <xsl:value-of select="$message[@id='directives']" />
    </a>

    <xsl:text> | </xsl:text>

    <a href="http://wiki.apache.org/httpd/FAQ">
        <xsl:value-of select="$message[@id='faq']" />
    </a>

    <xsl:text> | </xsl:text>

    <a href="{$path}/glossary.html">
        <xsl:value-of select="$message[@id='glossary']" />
    </a>

    <xsl:text> | </xsl:text>

    <a href="{$path}/sitemap.html">
        <xsl:value-of select="$message[@id='sitemap']" />
    </a>
</p>
</xsl:template>
<!-- /super-menu -->


<!-- ==================================================================== -->
<!-- <example>                                                            -->
<!-- iterate over *all* nodes; bare text and other inline stuff is        -->
<!-- wrapped into <p><code>, block level elements (defined in             -->
<!-- $blockelements) are applied "as is"                                  -->
<!-- ==================================================================== -->
<xsl:variable name="blocks"
            select="concat(' ', normalize-space($blockelements), ' ')" />

<xsl:template match="example">
<div class="example">
    <xsl:apply-templates select="title" mode="print" />

    <xsl:for-each select="./node()">
        <xsl:variable name="is-block-node"
                    select="boolean(contains($blocks,
                                             concat(' ', local-name(), ' ')))"/>
        <!-- bb = (number of) blocks nodes before (the current) -->
        <xsl:variable name="bb"
                    select="count(preceding-sibling::*[
                                contains($blocks,
                                         concat(' ', local-name(), ' '))])" />

        <xsl:if test="$is-block-node or position()=last()">
            <xsl:variable name="content">
                <!-- phew. short explanation, what happens here:      -->
                <!-- We want to get the inline stuff between the last -->
                <!-- block node and the current node.                 -->
                <!-- So filter all previous nodes for the condition   -->
                <!-- that the number of block nodes of all of *their* -->
                <!-- previous nodes is >= $bb. Hope that helps ;-)    -->
                <xsl:apply-templates
                    select="preceding-sibling::node()[
                                count(preceding-sibling::*[
                                    contains($blocks,
                                             concat(' ', local-name(), ' '))])
                                &gt;= $bb]" />

                <xsl:apply-templates
                    select="self::node()[not($is-block-node)]" />
            </xsl:variable>

            <!-- apply bare text only, if it's not only \s or empty -->
            <xsl:if test="not(normalize-space($content) = '')">
                <p><code>
                    <!-- same as $content above. xsl:copy-of seems to make -->
                    <!-- thread problems with xalan-j ... -->
                    <xsl:apply-templates
                        select="preceding-sibling::node()[
                                    count(preceding-sibling::*[
                                        contains($blocks,
                                                 concat(' ', local-name(),
                                                        ' '))])
                                    &gt;= $bb]" />

                    <xsl:apply-templates
                        select="self::node()[not($is-block-node)]" />
                </code></p>
            </xsl:if>

            <xsl:apply-templates select="self::node()[$is-block-node]" />
        </xsl:if>
    </xsl:for-each>
    <!-- /node() -->
</div> <!-- /.example -->
</xsl:template>
<!-- /example -->


<!-- ==================================================================== -->
<!-- example/title                                                        -->
<!-- ==================================================================== -->
<xsl:template match="example/title" mode="print">
<h3>
    <xsl:apply-templates/>
</h3>
</xsl:template>
<xsl:template match="example/title" />
<!-- /example/title -->


<!-- ==================================================================== -->
<!-- indentations                                                         -->
<!-- ==================================================================== -->
<xsl:template match="indent">
<span class="indent">
    <xsl:apply-templates/>
</span>
</xsl:template>
<!-- /indent -->


<!-- ==================================================================== -->
<!-- <note>                                                               -->
<!-- ==================================================================== -->
<xsl:template match="note">
<div class="note">
    <xsl:if test="@type='warning'">
        <xsl:attribute name="class">warning</xsl:attribute>
    </xsl:if>

    <xsl:apply-templates/>
</div>
</xsl:template>
<!-- /note -->


<!-- ==================================================================== -->
<!-- <note><title>                                                        -->
<!-- ==================================================================== -->
<xsl:template match="note/title">
<h3>
    <xsl:apply-templates/>
</h3>
</xsl:template>
<!-- /note/title -->


<!-- ==================================================================== -->
<!-- <directive>                                                          -->
<!-- Inserts link to another directive, which might be in another module. -->
<!-- References are converted into lower case.                            -->
<!-- ==================================================================== -->
<xsl:template match="directive" name="directive">
<code class="directive">
    <xsl:choose>
    <xsl:when test="@module">
        <xsl:variable name="lowerdirective">
            <xsl:choose>
            <xsl:when test="@name">
                <xsl:value-of select="normalize-space(concat(translate(@name,
                                        $uppercase, $lowercase),@idtype))" />
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="normalize-space(concat(translate(.,
                                        $uppercase, $lowercase),@idtype))" />
            </xsl:otherwise>
            </xsl:choose>
        </xsl:variable>

        <xsl:choose>
        <!-- No link if within the block that describes the directive itself -->
        <xsl:when test="$in-modulesynopsis and normalize-space(.) = ancestor::directivesynopsis/name">
                <xsl:if test="@type='section'">&lt;</xsl:if>
                <xsl:value-of select="."/>
                <xsl:if test="@type='section'">&gt;</xsl:if>
                <xsl:message>Removing link to '<xsl:value-of select="."/>'</xsl:message>
        </xsl:when>
        <xsl:when test="$in-modulesynopsis and normalize-space(@module) = /modulesynopsis/name">
            <a href="#{$lowerdirective}">
                <xsl:if test="@type='section'">&lt;</xsl:if>
                <xsl:value-of select="."/>
                <xsl:if test="@type='section'">&gt;</xsl:if>
            </a>
        </xsl:when>
        <xsl:otherwise>
            <a href="{$path}/mod/{normalize-space(@module)}.html#{$lowerdirective}">
                <xsl:if test="@type='section'">&lt;</xsl:if>
                <xsl:value-of select="."/>
                <xsl:if test="@type='section'">&gt;</xsl:if>
            </a>
        </xsl:otherwise>
        </xsl:choose>
    </xsl:when>

    <xsl:otherwise>
        <xsl:if test="@type='section'">&lt;</xsl:if>
        <xsl:value-of select="."/>
        <xsl:if test="@type='section'">&gt;</xsl:if>
        <!-- Missing module reference -->
        <xsl:choose>
            <!-- within another directive synopsis -->
            <xsl:when test="normalize-space(.) != ancestor::directivesynopsis/name">
                <xsl:message>link to '<xsl:value-of select="."/>' directive could be added in directive '<xsl:value-of select="ancestor::directivesynopsis/name"/>'</xsl:message>
            </xsl:when>
            <!-- somewhere else (try to find module name to give a hint) -->
            <xsl:when test="not(ancestor::directivesynopsis/name)">
                <xsl:message>link to '<xsl:value-of select="."/>' directive could be added in MODULE '<xsl:value-of select="/modulesynopsis/name"/>'</xsl:message>
            </xsl:when>
        </xsl:choose>
    </xsl:otherwise>
    </xsl:choose>
</code>
</xsl:template>
<!-- /directive -->


<!-- ==================================================================== -->
<!-- <module>                                                             -->
<!-- Inserts a link to refereed module                                    -->
<!-- ==================================================================== -->
<xsl:template match="module" name="module">
<code class="module">
    <xsl:choose>
    <xsl:when test="@outdated = 'true'">
        <xsl:value-of select="."/>
    </xsl:when>
    <xsl:otherwise>
        <a href="{$path}/mod/{normalize-space(.)}.html">
            <xsl:value-of select="."/>
        </a>
    </xsl:otherwise>
    </xsl:choose>
</code>
</xsl:template>
<!-- /module -->


<!-- ==================================================================== -->
<!-- <program>                                                            -->
<!-- Inserts a link to referred program                                   -->
<!-- ==================================================================== -->
<xsl:template match="program">
<code class="program">
    <a href="{$path}/programs/{normalize-space(.)}.html">
        <xsl:value-of select="normalize-space(.)" />
    </a>
</code>
</xsl:template>
<!-- /program -->


<!-- ==================================================================== -->
<!-- <related>                                                            -->
<!-- ==================================================================== -->
<xsl:template match="related">
<table class="related">
<tr>
    <th>
        <xsl:value-of select="$message[@id='relatedmodules']" />
    </th>
    <th>
        <xsl:value-of select="$message[@id='relateddirectives']" />
    </th>
</tr>
<tr>
    <td>
        <xsl:if test="count(modulelist/*) &gt; 0">
            <ul>
                <xsl:apply-templates select="modulelist" />
            </ul>
        </xsl:if>
    </td>
    <td>
        <xsl:if test="count(directivelist/*) &gt; 0">
            <ul>
                <xsl:apply-templates select="directivelist"/>
            </ul>
        </xsl:if>
    </td>
</tr>
</table>
</xsl:template>
<!-- /related -->

<!-- ==================================================================== -->
<!-- related/modulelist                                                   -->
<!-- ==================================================================== -->
<xsl:template match="related/modulelist">
<xsl:for-each select="module">
    <li>
        <xsl:call-template name="module"/>
    </li>
</xsl:for-each>
</xsl:template>
<!-- /related/modulelist -->


<!-- ==================================================================== -->
<!-- related/directivelist                                                -->
<!-- ==================================================================== -->
<xsl:template match="related/directivelist">
<xsl:for-each select="directive">
    <li>
        <xsl:call-template name="directive"/>
    </li>
</xsl:for-each>
</xsl:template>
<!-- /related/directivelist -->


<!-- ==================================================================== -->
<!-- <table>                                                              -->
<!-- ==================================================================== -->
<xsl:template match="table">
<table>
    <!-- existing border attribute will result in <table class="bordered"> -->
    <xsl:if test="@border">
        <xsl:attribute name="class">bordered</xsl:attribute>
    </xsl:if>

    <xsl:choose>
    <xsl:when test="@style = 'zebra'">
        <xsl:apply-templates select="tr" mode="zebra-table" />
    </xsl:when>
    <xsl:when test="@style = 'data'">
        <xsl:apply-templates select="tr" mode="data-table" />
    </xsl:when>
    <xsl:otherwise>
        <xsl:apply-templates />
    </xsl:otherwise>
    </xsl:choose>
</table>
</xsl:template>
<!-- /table -->

<!-- data-table -->
<xsl:template match="tr" mode="data-table">
<!-- style="data": fixed font, padding-left and right alignment for <td>s -->
<xsl:variable name="cross-table" select="boolean(
    preceding-sibling::tr/th[1]|following-sibling::tr/th[1])" />

<tr>
    <xsl:for-each select="node()">
        <xsl:choose>
        <xsl:when test="local-name() = 'td'">
            <td class="data">
                <xsl:apply-templates select="*|@*|text()" />
            </td>
        </xsl:when>
        <xsl:when test="local-name() = 'th' and
                        (not($cross-table) or
                         count(preceding-sibling::*) &gt; 0)">
            <th class="data">
                <xsl:apply-templates select="*|@*|text()" />
            </th>
        </xsl:when>
        <xsl:otherwise>
            <xsl:apply-templates select="self::node()" />
        </xsl:otherwise>
        </xsl:choose>
    </xsl:for-each>
</tr>&lf;
</xsl:template>


<!-- zebra-table -->
<xsl:template match="tr" mode="zebra-table">
<!-- style="zebra": alternating colors per row, i.e. every second row -->
<!--                gets a class="odd". Header lines (no <td>) get a  -->
<!--                class="header". These lines will be excluded from -->
<!--                the "odd" line count. That way header lines act   -->
<!--                interjectional, which creates a better visual and -->
<!--                psychological effect.                             -->
<tr>
    <xsl:choose>
    <xsl:when test="count(td) = 0">
        <xsl:attribute name="class">header</xsl:attribute>
    </xsl:when>

    <xsl:when test="position() mod 2 = (count(preceding-sibling::tr[count(td) = 0]) mod 2)">
        <xsl:attribute name="class">odd</xsl:attribute>
    </xsl:when>
    </xsl:choose>

    <xsl:apply-templates />
</tr>&lf;
</xsl:template>
<!-- /zebra-table -->


<!-- ==================================================================== -->
<!-- <ol>                                                                 -->
<!-- ==================================================================== -->
<xsl:template match="ol">
<ol>
    <!-- A. B. C. D. (list-style-type="upper-alpha") -->
    <xsl:choose>
    <xsl:when test="@type = 'A'">
        <xsl:attribute name="class">up-A</xsl:attribute>
    </xsl:when>
    <xsl:when test="@type = 'a'">
        <xsl:attribute name="class">lo-A</xsl:attribute>
    </xsl:when>
    </xsl:choose>

    <xsl:apply-templates/>
</ol>
</xsl:template>
<!-- /ol -->


<!-- ==================================================================== -->
<!-- diverse elements                                                     -->
<!-- Passes through content                                               -->
<!-- ==================================================================== -->
<xsl:template match="summary|description|usage|syntax|default">
<xsl:apply-templates/>
</xsl:template>
<!-- /diverse -->


<!-- ==================================================================== -->
<!-- <a>                                                                  -->
<!-- ==================================================================== -->
<xsl:template match="a">
<xsl:choose>
<xsl:when test="not(@href)">
    <xsl:copy>
        <xsl:apply-templates select="@*|*|text()"/>
    </xsl:copy>
</xsl:when>
<xsl:otherwise>
    <a href="@href">
        <xsl:apply-templates select="@*"/>
        <xsl:call-template name="helper.uri.fix">
            <xsl:with-param name="uri" select="@href"/>
        </xsl:call-template>

        <xsl:apply-templates select="*|text()"/>
    </a>
</xsl:otherwise>
</xsl:choose>
</xsl:template> 
<!-- /a -->


<!-- ==================================================================== -->
<!-- toplink                                                              -->
<!-- ==================================================================== -->
<xsl:template name="toplink">
<div class="top">
    <a href="#page-header"><img src="{$path}/images/up.gif" alt="top" /></a>
</div>
</xsl:template>
<!-- /toplink -->


<!-- ==================================================================== -->
<!-- <transnote>                                                          -->
<!-- translator's notes are displayed in a different color                -->
<!-- ==================================================================== -->
<xsl:template match="transnote">
<span class="transnote">
    <xsl:text>(</xsl:text>
    <em>
        <xsl:value-of select="$message[@id='transnote']" />
    </em>
    <xsl:text> </xsl:text>
    <xsl:apply-templates />
    <xsl:text>)</xsl:text>
</span>
</xsl:template>
<!-- /transnote -->

<!-- ==================================================================== -->
<!-- <phonetic>                                                           -->
<!-- phonetics are enclosed in  square brackets and displayed in a        -->
<!-- different color                                                      -->
<!-- ==================================================================== -->
<xsl:template match="phonetic">
<span class="phonetic">
    <xsl:text>[</xsl:text>
    <xsl:apply-templates />
    <xsl:text>]</xsl:text>
</span>
</xsl:template>
<!-- /phonetic -->


<!-- ==================================================================== -->
<!-- <glossary>                                                           -->
<!-- link to a glossary anchor                                            -->
<!-- ==================================================================== -->
<xsl:template match="glossary">
  <xsl:variable name="glosslink">
    <xsl:choose>
    <xsl:when test="@ref">
      <xsl:value-of select="@ref"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="translate(., $uppercase, $lowercase)"/>
    </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <a href="{$path}/glossary.html#{$glosslink}" class="glossarylink">
     <xsl:attribute name="title">
        <xsl:value-of select="$message[@id='glossarylink']" />
     </xsl:attribute>
     <xsl:apply-templates />
  </a>
</xsl:template>
<!-- /glossary -->

<!-- ==================================================================== -->
<!-- Filter &#160; in text() nodes.                                       -->
<!-- In some environments this character won't be transformed correctly,  -->
<!-- so we just write it explicitly as "&nbsp;" into the output.         -->
<!-- ==================================================================== -->
<xsl:template match="text()" name="filter.nbsp">
<xsl:param name="text" select="." />

<xsl:choose>
<xsl:when test="contains($text, '&#160;')">
    <xsl:value-of select="substring-before($text, '&#160;')" />
    &nbsp;
    <xsl:call-template name="filter.nbsp">
        <xsl:with-param name="text" select="substring-after($text, '&#160;')" />
    </xsl:call-template>
</xsl:when>
<xsl:otherwise>
    <xsl:value-of select="$text" />
</xsl:otherwise>
</xsl:choose>
</xsl:template>
<!-- /filter.nbsp -->


<!-- ==================================================================== -->
<!-- Process everything else by just passing it through                   -->
<!-- ==================================================================== -->
<xsl:template match="*">
<xsl:message terminate="yes">
    <xsl:text>Unknown element: </xsl:text>
    <xsl:value-of select="local-name()" />&lf;
    <xsl:text>Is the document valid (try `build validate-xml`)?</xsl:text>
</xsl:message>
</xsl:template>
<xsl:template match="@*"><xsl:copy /></xsl:template>
<xsl:template match="br"><br /></xsl:template>
<xsl:template match="tr"><tr><xsl:apply-templates select="*|@*|text()" /></tr></xsl:template>
<xsl:template match="th"><th><xsl:apply-templates select="*|@*|text()" /></th></xsl:template>
<xsl:template match="td"><td><xsl:apply-templates select="*|@*|text()" /></td></xsl:template>
<xsl:template match="p"><p><xsl:apply-templates select="*|@*|text()" /></p></xsl:template>
<xsl:template match="ul"><ul><xsl:apply-templates select="*|@*|text()" /></ul></xsl:template>
<xsl:template match="li"><li><xsl:apply-templates select="*|@*|text()" /></li></xsl:template>
<xsl:template match="dl"><dl><xsl:apply-templates select="*|@*|text()" /></dl></xsl:template>
<xsl:template match="dt"><dt><xsl:apply-templates select="*|@*|text()" /></dt></xsl:template>
<xsl:template match="dd"><dd><xsl:apply-templates select="*|@*|text()" /></dd></xsl:template>
<xsl:template match="em"><em><xsl:apply-templates select="*|@*|text()" /></em></xsl:template>
<xsl:template match="strong"><strong><xsl:apply-templates select="*|@*|text()" /></strong></xsl:template>
<xsl:template match="pre"><pre><xsl:call-template name="pre" /></pre></xsl:template>
<xsl:template match="code"><code><xsl:apply-templates select="*|@*|text()" /></code></xsl:template>
<xsl:template match="var"><var><xsl:apply-templates select="*|@*|text()" /></var></xsl:template>
<xsl:template match="dfn"><dfn><xsl:apply-templates select="*|@*|text()" /></dfn></xsl:template>
<xsl:template match="blockquote"><blockquote><xsl:apply-templates select="*|@*|text()" /></blockquote></xsl:template>
<xsl:template match="q"><q><xsl:apply-templates select="*|@*|text()" /></q></xsl:template>
<xsl:template match="cite"><cite><xsl:apply-templates select="*|@*|text()" /></cite></xsl:template>
<xsl:template match="img"><img><xsl:apply-templates select="*|@*|text()" /></img></xsl:template>
<!-- /pass through -->


<!-- ==================================================================== -->
<!-- create a letter bar                                                  -->
<!-- ==================================================================== -->
<xsl:template name="letter-bar">
<xsl:param name="letters" />
<xsl:param name="first" />

<xsl:if test="not($first)">
    <xsl:text> | </xsl:text>
</xsl:if>

<a href="#{substring($letters,1,1)}">
    &nbsp;
    <xsl:value-of select="substring($letters, 1, 1)" />
    &nbsp;
</a>

<xsl:if test="string-length($letters) &gt; 1">
    <xsl:call-template name="letter-bar">
        <xsl:with-param name="letters" select="substring($letters, 2)" />
        <xsl:with-param name="first" select="false()" />
    </xsl:call-template>
</xsl:if>
</xsl:template>
<!-- /letter-bar -->


<!-- ==================================================================== -->
<!-- template(s) for collecting all start letters of directives           -->
<!-- ==================================================================== -->
<xsl:template name="directive-startletters">
<xsl:param name="directives" />

<xsl:call-template name="_squeeze-letters">
    <xsl:with-param name="lastletter" select="''" />
    <xsl:with-param name="letters">
        <xsl:for-each select="$directives">
        <xsl:sort select="name"/>
            <xsl:value-of
                select="translate(substring(normalize-space(name), 1, 1),
                                  $lowercase, $uppercase)" />
        </xsl:for-each>
    </xsl:with-param>
</xsl:call-template>
</xsl:template>
<!-- /directive-startletters -->


<!-- ==================================================================== -->
<!-- squeeze subsequent letters in a string                               -->
<!-- ==================================================================== -->
<xsl:template name="_squeeze-letters">
<xsl:param name="letters"/>
<xsl:param name="lastletter"/>

<xsl:variable name="current" select="substring($letters, 1, 1)" />

<xsl:if test="not($lastletter = $current)">
    <xsl:value-of select="$current" />
</xsl:if>

<xsl:if test="string-length($letters) &gt; 1">
    <xsl:call-template name="_squeeze-letters">
        <xsl:with-param name="letters" select="substring($letters, 2)" />
        <xsl:with-param name="lastletter" select="$current"/>
    </xsl:call-template>
</xsl:if>
</xsl:template>
<!-- /_squeeze-letters -->


<!-- ==================================================================== -->
<!-- fix href and target attribute of an element.                         -->
<!-- ==================================================================== -->
<xsl:template name="helper.uri.fix">
<xsl:param name="uri"/>

<xsl:choose>
<!-- lame is_absolute_uri test -->
<xsl:when test="    contains($uri, ':')
                and string-length(substring-before($uri, ':')) &lt; 7">
    <xsl:if test="$ext-target">
        <xsl:attribute name="target">_blank</xsl:attribute>
    </xsl:if>
</xsl:when>
<xsl:otherwise>
    <xsl:variable name="fragment">
        <xsl:if test="contains($uri, '#')">
            <xsl:value-of select="concat('#', substring-after($uri, '#'))"/>
        </xsl:if>
    </xsl:variable>
    <xsl:variable name="absuri">
        <xsl:choose>
        <xsl:when test="contains($uri, '#')">
            <xsl:value-of select="concat('#', substring-before($uri, '#'))"/>
        </xsl:when>
        <xsl:otherwise>
            <xsl:value-of select="$uri"/>
        </xsl:otherwise>
        </xsl:choose>
    </xsl:variable>
        
    <xsl:if test="substring($absuri, string-length($uri), 1) = '/'">
        <xsl:attribute name="href">
            <xsl:value-of select="concat($absuri, $index-file, $fragment)"/>
        </xsl:attribute>
    </xsl:if>
</xsl:otherwise>
</xsl:choose>
</xsl:template>
<!-- /helper.uri.fix -->


<!-- ==================================================================== -->
<!-- Ignore table hints used for latex                                    -->
<!-- ==================================================================== -->
<xsl:template match="columnspec">
</xsl:template>

<xsl:template match="column">
</xsl:template>

</xsl:stylesheet>
