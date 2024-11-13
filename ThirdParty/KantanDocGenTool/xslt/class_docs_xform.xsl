<?xml version="1.0" encoding="ISO-8859-1"?>

<xsl:stylesheet
xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
xmlns="http://www.w3.org/TR/REC-html40"
version="2.0">

	<xsl:output method="html"/>

	<!-- Root template -->
	<xsl:template match="/">
		<html>
			<head>
				<title><xsl:value-of select="/root/display_name" /></title>
				<link rel="stylesheet" type="text/css" href="../css/bpdoc.css" />
			</head>
			<body>
				<div id="content_container">
					<xsl:apply-templates select="/root" />
				</div>
			</body>
		</html>
	</xsl:template>
	
	<xsl:template match="/root">
		<a class="navbar_style">
			<xsl:attribute name="href">../index.html</xsl:attribute>
			<xsl:value-of select="docs_name" />
		</a>
		<a class="navbar_style">&gt;</a>
		<a class="navbar_style"><xsl:value-of select="display_name" /></a>
		<h1 class="title_style"><xsl:value-of select="display_name" /></h1>
		<p><xsl:value-of select="description" /></p>
		<xsl:apply-templates select="inheritance" />
		<xsl:apply-templates select="interfaces" />
		<xsl:apply-templates select="references" />
		<xsl:apply-templates select="nodes" />
	</xsl:template>
	
	<xsl:template match="inheritance">
		<h3 class="title_style">Inheritance Hierarchy</h3>
		<table>
			<tbody>
				<xsl:apply-templates select="superClass" />
			</tbody>
		</table>	
	</xsl:template>
	
	<xsl:template match="superClass">
		<tr>
			<td>
				<xsl:variable name="left_pad" select="format-number((position())*5, '0')" />
				<xsl:attribute name="style">
					padding-left: <xsl:value-of select="$left_pad"/>px;
				</xsl:attribute>
				<a>
					<xsl:if test="id">
						<xsl:attribute name="href">
							../<xsl:value-of select="id" />/<xsl:value-of select="id" />.html
						</xsl:attribute>
					</xsl:if>
					<xsl:apply-templates select="display_name" />	
				</a>
			</td>
		</tr>
	</xsl:template>
	
	<xsl:template match="interfaces">
		<h3 class="title_style">Implemented Interfaces</h3>
		<table>
			<tbody>
				<xsl:apply-templates select="interface" />
			</tbody>
		</table>	
	</xsl:template>
	
	<xsl:template match="interface">
		<tr>
			<td>
				<a>
					<xsl:if test="id">
						<xsl:attribute name="href">
							../<xsl:value-of select="id" />/<xsl:value-of select="id" />.html
						</xsl:attribute>
					</xsl:if>
					<xsl:apply-templates select="display_name" />	
				</a>
			</td>
		</tr>
	</xsl:template>
	
	<xsl:template match="references">
		<h3 class="title_style">References</h3>
		<table>
			<tbody>
				<xsl:if test="module">
					<tr>
						<td>
							<b>Module</b>
						</td>
						<td>
							<xsl:value-of select="module" />
						</td>
					</tr>
				</xsl:if>
				<xsl:if test="header">
					<tr>
						<td>
							<b>Header</b>
						</td>
						<td>
							<xsl:value-of select="header" />
						</td>
					</tr>
				</xsl:if>
				<xsl:if test="source">
					<tr>
						<td>
							<b>Source</b>
						</td>
						<td>
							<xsl:value-of select="source" />
						</td>
					</tr>
				</xsl:if>
				<xsl:if test="include">
					<tr>
						<td>
							<b>Include</b>
						</td>
						<td>
							<xsl:value-of select="include" />
						</td>
					</tr>
				</xsl:if>
			</tbody>
		</table>	
	</xsl:template>

	<!-- Templates to match specific elements in the input xml -->
	<xsl:template match="nodes">
		<h3 class="title_style">Functions</h3>
		<table>
			<tbody>
				<xsl:apply-templates select="node">
					<xsl:sort select="shorttitle"/>
				</xsl:apply-templates>
			</tbody>
		</table>	
	</xsl:template>

	<xsl:template match="node">
		<tr>
			<td>
				<a>
					<xsl:attribute name="href">./nodes/<xsl:value-of select="id" />.html</xsl:attribute>
					<xsl:apply-templates select="shorttitle" />	
				</a>
			</td>
			<td>
				<xsl:apply-templates select="description" />	
			</td>
		</tr>
	</xsl:template>

</xsl:stylesheet>
