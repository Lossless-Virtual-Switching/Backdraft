<?xml version="1.0"?>
<!DOCTYPE xsl:stylesheet [
    <!ENTITY lf SYSTEM "../xsl/util/lf.xml">
]>

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

<xsl:stylesheet version="1.0"
              xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                  xmlns="http://www.w3.org/1999/xhtml">


<!-- load utility snippets -->
<xsl:include href="../xsl/util/pretrim.xsl" />

<!-- ==================================================================== -->
<!-- Ordinary HTML that must be converted to latex                        -->
<!-- ==================================================================== -->

<xsl:template match="ul">
<xsl:text>\begin{itemize}</xsl:text>&lf;
<xsl:apply-templates/>
<xsl:text>\end{itemize}</xsl:text>&lf;
</xsl:template>

<xsl:template match="ol">
<xsl:text>\begin{enumerate}</xsl:text>&lf;
<xsl:apply-templates/>
<xsl:text>\end{enumerate}</xsl:text>&lf;
</xsl:template>

<xsl:template match="li">
<xsl:text>\item </xsl:text>
<xsl:apply-templates/>
&lf;
</xsl:template>

<xsl:template match="dl">
<xsl:text>\begin{description}</xsl:text>&lf;
<xsl:apply-templates/>
<xsl:text>\end{description}</xsl:text>&lf;
</xsl:template>

<xsl:template match="dt">
<xsl:text>\item[</xsl:text><xsl:apply-templates/>
<xsl:text>] </xsl:text>
</xsl:template>

<xsl:template match="dd">
<xsl:apply-templates/>
</xsl:template>

<xsl:template match="br">
<xsl:call-template name="br">
<xsl:with-param name="result" select="'\\'" />
</xsl:call-template>
</xsl:template>

<xsl:template match="br" mode="tabular">
<xsl:call-template name="br">
<xsl:with-param name="result" select="'\newline'" />
</xsl:call-template>
</xsl:template>

<!-- Latex doesn't like successive line breaks, so replace any
     sequence of two or more br separated only by white-space with
     one line break followed by smallskips. -->
<xsl:template name="br">
<xsl:param name="result" />

<xsl:choose>
<xsl:when test="name(preceding-sibling::node()[1])='br' or name(preceding-sibling::node()[1])='indent'">
<xsl:text>\smallskip </xsl:text>
</xsl:when>
<xsl:when test="name(preceding-sibling::node()[2])='br' or name(preceding-sibling::node()[2])='indent'">
  <xsl:choose>
  <xsl:when test="normalize-space(preceding-sibling::node()[1])=''">
    <xsl:text>\smallskip </xsl:text>
  </xsl:when>
  <xsl:otherwise>
    <!-- Don't put a line break if we are the last thing -->
    <xsl:if test="not(position()=last()) and not(position()=last()-1 and normalize-space(following-sibling::node()[1])='')">
      <xsl:value-of select="$result" />
      <xsl:text> </xsl:text>
    </xsl:if>
  </xsl:otherwise>
  </xsl:choose>
</xsl:when>
<xsl:otherwise>
    <!-- Don't put a line break if we are the last thing -->
    <xsl:if test="not(position()=last()) and not(position()=last()-1 and normalize-space(following-sibling::node()[1])='')">
      <xsl:value-of select="$result" />
      <xsl:text> </xsl:text>
    </xsl:if>
</xsl:otherwise>
</xsl:choose>
</xsl:template>

<xsl:template match="p">
<xsl:apply-templates/>
<xsl:text>\par</xsl:text>&lf;
</xsl:template>

<xsl:template match="code|program">
<xsl:text>\texttt{</xsl:text>
<xsl:apply-templates/>
<xsl:text>}</xsl:text>
</xsl:template>

<xsl:template match="code|program" mode="tabular">
<xsl:text>\texttt{</xsl:text>
<xsl:apply-templates mode="tabular"/>
<xsl:text>}</xsl:text>
</xsl:template>

<xsl:template match="strong">
<xsl:text>\textbf{</xsl:text>
<xsl:apply-templates/>
<xsl:text>}</xsl:text>
</xsl:template>

<xsl:template match="strong" mode="tabular">
<xsl:text>\textbf{</xsl:text>
<xsl:apply-templates mode="tabular"/>
<xsl:text>}</xsl:text>
</xsl:template>

<xsl:template match="em|var|cite|q|dfn">
<xsl:text>\textit{</xsl:text>
<xsl:apply-templates mode="tabular"/>
<xsl:text>}</xsl:text>
</xsl:template>


<!-- Value-of used here explicitly because we don't want latex-escaping
performed.  Of course, this will conflict with html where some tags are
interpreted in pre -->
<xsl:template match="pre|highlight">
<xsl:text>\begin{verbatim}</xsl:text>

<!-- If it's a one-liner, trim the initial indentation as well -->
<!-- it's most likely an accident                              -->
<xsl:call-template name="pre-ltrim-one">
  <xsl:with-param name="string">
    <xsl:call-template name="pre-rtrim">
      <xsl:with-param name="string">
        <xsl:call-template name="pre-ltrim">
          <xsl:with-param name="string">
            <xsl:value-of select="." />
          </xsl:with-param>
        </xsl:call-template>
      </xsl:with-param>
    </xsl:call-template>
  </xsl:with-param>
</xsl:call-template>

<xsl:text>\end{verbatim}</xsl:text>&lf;
</xsl:template>

<xsl:template match="blockquote">
<xsl:text>\begin{quotation}</xsl:text>&lf;
<xsl:apply-templates/>
<xsl:text>\end{quotation}</xsl:text>&lf;
</xsl:template>

<!-- XXX: We need to deal with table headers -->

<xsl:template match="table">
<xsl:variable name="table-type">
  <xsl:choose>
  <xsl:when test="count(tr) &gt; 15">longtable</xsl:when>
  <xsl:otherwise>tabular</xsl:otherwise>
  </xsl:choose>
</xsl:variable>

<xsl:text>\begin{</xsl:text><xsl:value-of select="$table-type"/>
<xsl:text>}{|</xsl:text>
<xsl:choose>
<xsl:when test="columnspec">
  <xsl:for-each select="columnspec/column">
    <xsl:text>l</xsl:text>
    <xsl:if test="../../@border and not(position()=last())">
      <xsl:text>|</xsl:text>
    </xsl:if>
  </xsl:for-each>
</xsl:when>
<xsl:otherwise>
  <xsl:for-each select="tr[1]/*">
    <xsl:text>l</xsl:text>
    <xsl:if test="../../@border and not(position()=last())">
      <xsl:text>|</xsl:text>
    </xsl:if>
  </xsl:for-each>
</xsl:otherwise>
</xsl:choose>
<xsl:text>|}\hline</xsl:text>&lf;
<xsl:apply-templates select="tr"/>
<xsl:text>\hline\end{</xsl:text>
<xsl:value-of select="$table-type"/>
<xsl:text>}</xsl:text>&lf;
</xsl:template>

<xsl:template match="tr">
  <xsl:apply-templates select="td|th"/>
  <xsl:text>\\</xsl:text>
  <xsl:if test="../@border and not(position()=last())">
    <xsl:text>\hline</xsl:text>
  </xsl:if>
  &lf;
</xsl:template>

<xsl:template match="td">
    <xsl:variable name="pos" select="position()"/>
    <xsl:text>\begin{minipage}[t]{</xsl:text>
    <xsl:choose>
    <xsl:when test="../../columnspec">
      <xsl:value-of select="../../columnspec/column[$pos]/@width"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select=".95 div last()"/>
    </xsl:otherwise>
    </xsl:choose>
    <xsl:text>\textwidth}\small </xsl:text>
    <xsl:apply-templates/>
    <xsl:text>\end{minipage}</xsl:text>
    <xsl:if test="not(position()=last())">
      <xsl:text> &amp; </xsl:text>
    </xsl:if>
</xsl:template>

<xsl:template match="th">
    <xsl:variable name="pos" select="position()"/>
    <xsl:text>\begin{minipage}[t]{</xsl:text>
    <xsl:choose>
    <xsl:when test="../../columnspec">
      <xsl:value-of select="../../columnspec/column[$pos]/@width"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select=".95 div last()"/>
    </xsl:otherwise>
    </xsl:choose>
    <xsl:text>\textwidth}\bfseries </xsl:text>
    <xsl:apply-templates/>
    <xsl:text>\end{minipage}</xsl:text>
    <xsl:if test="not(position()=last())">
      <xsl:text> &amp; </xsl:text>
    </xsl:if>
</xsl:template>

<!--
   This is a horrible hack, but it seems to mostly work.  It does a
   few things:

   1. Transforms references starting in http:// to footnotes with the
      appropriate hyperref macro to make them clickable.  (This needs
      to be expanded to deal with news: and needs to be adjusted to
      deal with "#", which is creating bad links at the moment.)

   2. For intra-document references, constructs the appropriate absolute
      reference using a latex \pageref.  
      This involves applying a simplified version of the
      general URL resolution rules to deal with ../.  It only works for
      one level of subdirectory.

   3. It is also necessary to deal with the fact that index pages
      get references as "/".
-->
<xsl:template match="a" mode="tabular">
<xsl:apply-templates mode="tabular"/>
<xsl:call-template name="a"/>
</xsl:template>

<xsl:template match="a">
<xsl:apply-templates/>
<xsl:call-template name="a"/>
</xsl:template>

<xsl:template name="a">
<xsl:if test="@href">
<xsl:variable name="relpath" select="document(/*/@metafile)/metafile/relpath" />
<xsl:variable name="path" select="document(/*/@metafile)/metafile/path" />
<xsl:variable name="href">
  <xsl:choose>
  <xsl:when test="starts-with(@href, './')">
    <xsl:value-of select="substring(@href, 3)" />
  </xsl:when>
  <xsl:otherwise>
    <xsl:value-of select="@href" />
  </xsl:otherwise>
  </xsl:choose>
</xsl:variable>
<xsl:variable name="fileref">
  <xsl:choose>
  <xsl:when test="contains($href, '.html')">
    <xsl:value-of select="substring-before($href, '.html')"/>
  </xsl:when>
  <xsl:otherwise>
    <xsl:value-of select="concat($href, 'index')"/>
  </xsl:otherwise>
  </xsl:choose>
</xsl:variable>
<xsl:choose>

<xsl:when test="starts-with(@href, 'http:') or starts-with(@href, 'https:') or starts-with(@href, 'ftp:') or starts-with(@href, 'news:') or starts-with(@href, 'mailto:')">
  <xsl:if test="not(.=@href)">
    <xsl:text>\footnote{</xsl:text>
      <xsl:text>\href{</xsl:text>
      <xsl:call-template name="replace-string">
        <xsl:with-param name="replace" select="'%'"/>
        <xsl:with-param name="with" select="'\%'"/>
        <xsl:with-param name="text">
          <xsl:call-template name="replace-string">
            <xsl:with-param name="replace" select="'_'"/>
            <xsl:with-param name="with" select="'\_'"/>
            <xsl:with-param name="text">
              <xsl:call-template name="replace-string">
                <xsl:with-param name="replace" select="'#'"/>
                <xsl:with-param name="with" select="'\#'"/>
                <xsl:with-param name="text" select="@href"/>
              </xsl:call-template>
            </xsl:with-param>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
      <xsl:text>}{</xsl:text>
    <xsl:call-template name="ltescape">
      <xsl:with-param name="string" select="@href"/>
    </xsl:call-template>
    <xsl:text>}}</xsl:text>
  </xsl:if>
</xsl:when>
<xsl:when test="starts-with(@href, '#')">
<!-- Don't do inter-section references -->
</xsl:when>
<xsl:otherwise>
  <xsl:text> (p.\ \pageref{</xsl:text>
    <xsl:call-template name="replace-string">
      <xsl:with-param name="replace" select="'#'"/>
      <xsl:with-param name="with" select="':'"/>
      <xsl:with-param name="text">
      <xsl:choose>
      <xsl:when test="$relpath='.'">
        <xsl:value-of select="concat('/',$fileref)"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:choose>
        <xsl:when test="starts-with($fileref,'..')">
          <xsl:value-of select="substring-after($fileref,'..')"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="concat($path,$fileref)"/>
        </xsl:otherwise>
        </xsl:choose>
      </xsl:otherwise>
      </xsl:choose>
      </xsl:with-param>
     </xsl:call-template>
  <xsl:text>}) </xsl:text>
</xsl:otherwise>
</xsl:choose>
</xsl:if>
</xsl:template>

<xsl:template match="img">
<xsl:call-template name="img"/>
</xsl:template>

<xsl:template match="img" mode="tabular">
<xsl:call-template name="img"/>
</xsl:template>

<xsl:template name="img">
<xsl:variable name="path" select="document(/*/@metafile)/metafile/path" />
<xsl:text>\includegraphics{</xsl:text>
      <xsl:call-template name="replace-string">
        <xsl:with-param name="text" select="concat('.',$path,@src)"/>
        <xsl:with-param name="replace" select="'.gif'"/>
        <xsl:with-param name="with" select="''"/>
      </xsl:call-template>
<xsl:text>}</xsl:text>
</xsl:template>

</xsl:stylesheet>
