using System;
using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Lablr.Api.Data.Migrations
{
    /// <inheritdoc />
    public partial class InitialCreate : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.CreateTable(
                name: "labels",
                columns: table => new
                {
                    Id = table.Column<string>(type: "TEXT", nullable: false),
                    Name = table.Column<string>(type: "TEXT", nullable: false),
                    WidthMm = table.Column<double>(type: "REAL", nullable: false),
                    HeightMm = table.Column<double>(type: "REAL", nullable: false),
                    Material = table.Column<string>(type: "TEXT", nullable: true),
                    MarginsMm = table.Column<string>(type: "TEXT", nullable: true),
                    OffsetCorrectionMm = table.Column<string>(type: "TEXT", nullable: true),
                    CompatiblePrinters = table.Column<string>(type: "TEXT", nullable: true),
                    Manufacturer = table.Column<string>(type: "TEXT", nullable: true),
                    Sku = table.Column<string>(type: "TEXT", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_labels", x => x.Id);
                });

            migrationBuilder.CreateTable(
                name: "pictograms",
                columns: table => new
                {
                    Name = table.Column<string>(type: "TEXT", nullable: false),
                    Image = table.Column<string>(type: "TEXT", nullable: false),
                    Svg = table.Column<string>(type: "TEXT", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_pictograms", x => x.Name);
                });

            migrationBuilder.CreateTable(
                name: "print_agents",
                columns: table => new
                {
                    Id = table.Column<string>(type: "TEXT", nullable: false),
                    Name = table.Column<string>(type: "TEXT", nullable: false),
                    Token = table.Column<string>(type: "TEXT", nullable: false),
                    IsDefault = table.Column<bool>(type: "INTEGER", nullable: false),
                    CreatedAt = table.Column<DateTimeOffset>(type: "TEXT", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_print_agents", x => x.Id);
                });

            migrationBuilder.CreateTable(
                name: "printers",
                columns: table => new
                {
                    Id = table.Column<string>(type: "TEXT", nullable: false),
                    Name = table.Column<string>(type: "TEXT", nullable: false),
                    Dpi = table.Column<int>(type: "INTEGER", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_printers", x => x.Id);
                });

            migrationBuilder.CreateTable(
                name: "templates",
                columns: table => new
                {
                    Id = table.Column<string>(type: "TEXT", nullable: false),
                    Name = table.Column<string>(type: "TEXT", nullable: false),
                    Label = table.Column<string>(type: "TEXT", nullable: false),
                    RequiredFields = table.Column<string>(type: "TEXT", nullable: false),
                    OptionalFields = table.Column<string>(type: "TEXT", nullable: true),
                    Orientation = table.Column<string>(type: "TEXT", nullable: true),
                    Elements = table.Column<string>(type: "TEXT", nullable: true),
                    Variants = table.Column<string>(type: "TEXT", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_templates", x => x.Id);
                });
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropTable(
                name: "labels");

            migrationBuilder.DropTable(
                name: "pictograms");

            migrationBuilder.DropTable(
                name: "print_agents");

            migrationBuilder.DropTable(
                name: "printers");

            migrationBuilder.DropTable(
                name: "templates");
        }
    }
}
